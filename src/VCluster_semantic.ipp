/*
 * VCluster_semantic.hpp
 *
 * Implementation of semantic communications
 *
 *  Created on: Feb 8, 2016
 *      Author: Pietro Incardona
 */

private:

	// Structures that do an unpack, depending on the existence of max_prop inside 'send'

	//
	template<bool result, typename T, typename S>
	struct unpack_selector
	{
		template<int ... prp> static void call_unpack(S & recv, openfpm::vector<BHeapMemory> & recv_buf, openfpm::vector<size_t> * sz = NULL)
		{
#ifdef DEBUG
			std::cout << "Sz.size(): " << sz->size() << std::endl;
#endif						
			for (size_t i = 0 ; i < recv_buf.size() ; i++)
			{
#ifdef DEBUG				
				std::cout << "Recv_buf.get(i).size(): " << recv_buf.get(i).size() << std::endl;
#endif				
				T unp;

				ExtPreAlloc<HeapMemory> & mem = *(new ExtPreAlloc<HeapMemory>(recv_buf.get(i).size(),recv_buf.get(i)));
				mem.incRef();
				
				Unpack_stat ps;
				
				Unpacker<T,HeapMemory>::template unpack<prp...>(mem, unp, ps);
				
				size_t recv_size_old = recv.size();
				// Merge the information
				recv.add(unp);
				size_t recv_size_new = recv.size();
				
				if (sz != NULL)
					sz->get(i) = recv_size_new - recv_size_old;
			}
		}
	};

	
	//
	template<typename T, typename S>
	struct unpack_selector<true, T, S>
	{
		template<int ... prp> static void call_unpack(S & recv, openfpm::vector<BHeapMemory> & recv_buf, openfpm::vector<size_t> * sz = NULL)
		{
			for (size_t i = 0 ; i < recv_buf.size() ; i++)
			{

				/*ExtPreAlloc<HeapMemory> & mem = *(new ExtPreAlloc<HeapMemory>(recv_buf.get(i).size(),recv_buf.get(i)));
				mem.incRef();
				
				Unpack_stat ps;
				
				size_t n_ele = 0;
				Unpacker<size_t, HeapMemory>::unpack(mem,n_ele,ps);*/
				
				// calculate the number of received elements
				size_t n_ele = recv_buf.get(i).size() / sizeof(typename T::value_type);
				
				// add the received particles to the vector
				PtrMemory * ptr1 = new PtrMemory(recv_buf.get(i).getPointer(),recv_buf.get(i).size());
		
				// create vector representation to a piece of memory already allocated
				openfpm::vector<typename T::value_type,PtrMemory,typename memory_traits_lin<typename T::value_type>::type, memory_traits_lin,openfpm::grow_policy_identity> v2;
		
				v2.setMemory(*ptr1);
		
				// resize with the number of elements
				v2.resize(n_ele);
				
				// Merge the information
				
				size_t recv_size_old = recv.size();
				// Merge the information
				recv.add(v2);
				size_t recv_size_new = recv.size();
							
				if (sz != NULL)
					sz->get(i) = recv_size_new - recv_size_old;
			}
		}
	};
	
	
	
	

	template<typename T>
	struct call_serialize_variadic {};
	
	template<int ... prp>
	struct call_serialize_variadic<index_tuple<prp...>>
	{
		template<typename T> inline static void call_pr(T & send, size_t & tot_size)
		{
			Packer<T,HeapMemory>::template packRequest<prp...>(send,tot_size);
		}
		
		template<typename T> inline static void call_pack(ExtPreAlloc<HeapMemory> & mem, T & send, Pack_stat & sts)
		{
			Packer<T,HeapMemory>::template pack<prp...>(mem,send,sts);
		}
		
		template<typename T, typename S> inline static void call_unpack(S & recv, openfpm::vector<BHeapMemory> & recv_buf, openfpm::vector<size_t> * sz = NULL)
		{
			const bool result = has_pack_gen<typename T::value_type>::value == false && is_vector<T>::value == true;
			//const bool result = has_pack<typename T::value_type>::type::value == false && has_pack_agg<typename T::value_type>::result::value == false && is_vector<T>::value == true;
			unpack_selector<result, T, S>::template call_unpack<prp...>(recv, recv_buf, sz);
		}		
	};

	// Structures that do a pack request, depending on the existence of max_prop inside 'send'

	//There is max_prop inside
	template<bool cond, typename T, typename S>
	struct pack_unpack_cond
	{
		static void packingRequest(T & send, size_t & tot_size, openfpm::vector<size_t> & sz)
		{
			typedef typename ::generate_indexes<int, has_max_prop<T, has_value_type<T>::value>::number, MetaFuncOrd>::result ind_prop_to_pack;
			if (has_pack_gen<typename T::value_type>::value == false && is_vector<T>::value == true)
			//if (has_pack<typename T::value_type>::type::value == false && has_pack_agg<typename T::value_type>::result::value == false && is_vector<T>::value == true)
			{
#ifdef DEBUG
				std::cout << "Inside SGather pack request (has prp) (vector case) " << std::endl;
#endif
				sz.add(send.size()*sizeof(typename T::value_type));	
			}
			else
			{
				call_serialize_variadic<ind_prop_to_pack>::call_pr(send,tot_size);
#ifdef DEBUG
				std::cout << "Inside SGather pack request (has prp) (general case) " << std::endl;
#endif
				sz.add(tot_size);
			}
		}
		
		static void packing(ExtPreAlloc<HeapMemory> & mem, T & send, Pack_stat & sts, openfpm::vector<const void *> & send_buf)
		{
			typedef typename ::generate_indexes<int, has_max_prop<T, has_value_type<T>::value>::number, MetaFuncOrd>::result ind_prop_to_pack;
			if (has_pack_gen<typename T::value_type>::value == false && is_vector<T>::value == true)
			//if (has_pack<typename T::value_type>::type::value == false && has_pack_agg<typename T::value_type>::result::value == false && is_vector<T>::value == true)
			{
#ifdef DEBUG
				std::cout << "Inside SGather pack (has prp) (vector case) " << std::endl;
#endif
				//std::cout << demangle(typeid(T).name()) << std::endl;
				send_buf.add(send.getPointer());
			}
			else
			{
#ifdef DEBUG
				std::cout << "Inside SGather pack (has prp) (general case) " << std::endl;
#endif
				send_buf.add(mem.getPointerEnd());
				call_serialize_variadic<ind_prop_to_pack>::call_pack(mem,send,sts);			
			}			
		}
		
		static void unpacking(S & recv, openfpm::vector<BHeapMemory> & recv_buf, openfpm::vector<size_t> * sz = NULL)
		{
			typedef typename ::generate_indexes<int, has_max_prop<T, has_value_type<T>::value>::number, MetaFuncOrd>::result ind_prop_to_pack;
			call_serialize_variadic<ind_prop_to_pack>::template call_unpack<T,S>(recv, recv_buf, sz);
		}	
	};

	
	//There is no max_prop inside
	template<typename T, typename S>
	struct pack_unpack_cond<false, T, S>
	{
		static void packingRequest(T & send, size_t & tot_size, openfpm::vector<size_t> & sz)
		{

			if (has_pack<typename T::value_type>::type::value == false && is_vector<T>::value == true)
			{
#ifdef DEBUG				
				std::cout << "Inside SGather pack request (no prp) (vector case) " << std::endl;
#endif
				sz.add(send.size()*sizeof(typename T::value_type));			
			}
			
			else
			{
				Packer<T,HeapMemory>::packRequest(send,tot_size);	
				
#ifdef DEBUG
				std::cout << "Tot_size: " << tot_size << std::endl; 
#endif
				sz.add(tot_size);
			}
		}
		
		static void packing(ExtPreAlloc<HeapMemory> & mem, T & send, Pack_stat & sts, openfpm::vector<const void *> & send_buf)
		{

			if (has_pack<typename T::value_type>::type::value == false && is_vector<T>::value == true)
			{
#ifdef DEBUG
				std::cout << "Inside SGather pack (no prp) (vector case)" << std::endl;
#endif
				send_buf.add(send.getPointer());
			}
			
			else
			{
#ifdef DEBUG
				std::cout << "Inside SGather pack (no prp) (genaral case) " << std::endl;
#endif
				send_buf.add(mem.getPointerEnd());	
				Packer<T,HeapMemory>::pack(mem,send,sts);			
			}
		}

		static void unpacking(S & recv, openfpm::vector<BHeapMemory> & recv_buf, openfpm::vector<size_t> * sz = NULL)
		{
#ifdef DEBUG			
			std::cout << "Inside SGather unpack (no prp) " << std::endl;
#endif
			
			const bool result = has_pack<typename T::value_type>::type::value == false && is_vector<T>::value == true;	
		
			unpack_selector<result, T, S>::template call_unpack<>(recv, recv_buf, sz);
		}
	};


/*! \brief Reset the receive buffer
 * 
 * 
 */
void reset_recv_buf()
{
	for (size_t i = 0 ; i < recv_buf.size() ; i++)
		recv_buf.get(i).resize(0);

	recv_buf.resize(0);
}

/*! \brief Base info
 *
 * \param recv_buf receive buffers
 * \param prc processors involved
 * \param size of the received data
 *
 */
struct base_info
{
	openfpm::vector<BHeapMemory> * recv_buf;
	openfpm::vector<size_t> & prc;
	openfpm::vector<size_t> & sz;

	// constructor
	base_info(openfpm::vector<BHeapMemory> * recv_buf, openfpm::vector<size_t> & prc, openfpm::vector<size_t> & sz)
	:recv_buf(recv_buf),prc(prc),sz(sz)
	{}
};

/*! \brief Call-back to allocate buffer to receive data
 *
 * \param msg_i size required to receive the message from i
 * \param total_msg total size to receive from all the processors
 * \param total_p the total number of processor that want to communicate with you
 * \param i processor id
 * \param ri request id (it is an id that goes from 0 to total_p, and is unique
 *           every time message_alloc is called)
 * \param ptr a pointer to the vector_dist structure
 *
 * \return the pointer where to store the message for the processor i
 *
 */
static void * msg_alloc(size_t msg_i ,size_t total_msg, size_t total_p, size_t i, size_t ri, void * ptr)
{
	base_info & rinfo = *(base_info *)ptr;

	if (rinfo.recv_buf == NULL)
	{
		std::cerr << __FILE__ << ":" << __LINE__ << " Internal error this processor is not suppose to receive\n";
		return NULL;
	}

	rinfo.recv_buf->resize(ri+1);

	rinfo.recv_buf->get(ri).resize(msg_i);

	// Receive info
	rinfo.prc.add(i);
	rinfo.sz.add(msg_i);

	// return the pointer
	return rinfo.recv_buf->last().getPointer();
}

/*! \brief Process the receive buffer
 *
 * \tparam T type of sending object
 * \tparam S type of receiving object
 *
 * \param recv receive object
 *
 */
template<typename T, typename S> void process_receive_buffer(S & recv, openfpm::vector<size_t> * sz = NULL)
{
	if (sz != NULL)
		sz->resize(recv_buf.size());

	pack_unpack_cond<has_max_prop<T, has_value_type<T>::value>::value, T, S>::unpacking(recv, recv_buf, sz);
}

public:

/*! \brief Semantic Gather, gather the data from all processors into one node
 *
 * Semantic communication differ from the normal one. They in general 
 * follow the following model.
 * 
 * Gather(T,S,root,op=add);
 *
 * "Gather" indicate the communication pattern, or how the information flow
 * T is the object to send, S is the object that will receive the data. 
 * In order to work S must implement the interface S.add(T).
 *
 * ### Example send a vector of structures, and merge all together in one vector
 * \snippet VCluster_semantic_unit_tests.hpp Gather the data on master
 *
 * ### Example send a vector of structures, and merge all together in one vector
 * \snippet VCluster_semantic_unit_tests.hpp Gather the data on master complex
 *
 * \tparam T type of sending object
 * \tparam S type of receiving object
 *
 * \param Object to send
 * \param Object to receive
 * \param root witch node should collect the information
 *
 * \return true if the function completed succefully
 *
 */
template<typename T, typename S> bool SGather(T & send, S & recv,size_t root)
{
	openfpm::vector<size_t> prc;
	openfpm::vector<size_t> sz;

	return SGather(send,recv,prc,sz,root);
}

template<size_t index, size_t N> struct MetaFuncOrd {
   enum { value = index };
};

/*! \brief Semantic Gather, gather the data from all processors into one node
 *
 * Semantic communication differ from the normal one. They in general
 * follow the following model.
 *
 * Gather(T,S,root,op=add);
 *
 * "Gather" indicate the communication pattern, or how the information flow
 * T is the object to send, S is the object that will receive the data.
 * In order to work S must implement the interface S.add(T).
 *
 * ### Example send a vector of structures, and merge all together in one vector
 * \snippet VCluster_semantic_unit_tests.hpp Gather the data on master
 *
 * ### Example send a vector of structures, and merge all together in one vector
 * \snippet VCluster_semantic_unit_tests.hpp Gather the data on master complex
 *
 * \tparam T type of sending object
 * \tparam S type of receiving object
 *
 * \param Object to send
 * \param Object to receive
 * \param root witch node should collect the information
 * \param prc processors from witch we received the information
 * \param sz size of the received information for each processor
 *
 * \return true if the function completed succefully
 *
 */
template<typename T, typename S> bool SGather(T & send, S & recv, openfpm::vector<size_t> & prc, openfpm::vector<size_t> & sz,size_t root)
{
	// Reset the receive buffer
	reset_recv_buf();
	
	// If we are on master collect the information
	if (getProcessUnitID() == root)
	{
#ifdef DEBUG
		std::cout << "Inside root " << root << std::endl;
#endif
		// send buffer (master does not send anything) so send req and send_buf
		// remain buffer with size 0
		openfpm::vector<size_t> send_req;

		// receive information
		base_info bi(&recv_buf,prc,sz);

		// Send and recv multiple messages
		sendrecvMultipleMessagesNBX(send_req.size(),NULL,NULL,NULL,msg_alloc,&bi);

		// process the received information
		process_receive_buffer<T,S>(recv,&sz);

		recv.add(send);
		prc.add(root);
		sz.add(send.size());
	}
	else
	{
#ifdef DEBUG
		std::cout << "Inside slave " << getProcessUnitID() << std::endl;
#endif
		// send buffer (master does not send anything) so send req and send_buf
		// remain buffer with size 0
		openfpm::vector<size_t> send_prc;
		send_prc.add(root);
				
		openfpm::vector<size_t> sz;
				
		openfpm::vector<const void *> send_buf;
			
		//Pack requesting
		
		size_t tot_size = 0;
		
		pack_unpack_cond<has_max_prop<T, has_value_type<T>::value>::value, T, S>::packingRequest(send, tot_size, sz);
		
		HeapMemory pmem;
		
		ExtPreAlloc<HeapMemory> & mem = *(new ExtPreAlloc<HeapMemory>(tot_size,pmem));
		mem.incRef();

		//Packing

		Pack_stat sts;
		
		pack_unpack_cond<has_max_prop<T, has_value_type<T>::value>::value, T, S>::packing(mem, send, sts, send_buf);

		// receive information
		base_info bi(NULL,prc,sz);

		// Send and recv multiple messages
		sendrecvMultipleMessagesNBX(send_prc.size(),(size_t *)sz.getPointer(),(size_t *)send_prc.getPointer(),(void **)send_buf.getPointer(),msg_alloc,(void *)&bi);
	}
	
	return true;
}

/*! \brief Semantic Scatter, scatter the data from one processor to the other node
 *
 * Semantic communication differ from the normal one. They in general
 * follow the following model.
 *
 * Scatter(T,S,...,op=add);
 *
 * "Scatter" indicate the communication pattern, or how the information flow
 * T is the object to send, S is the object that will receive the data.
 * In order to work S must implement the interface S.add(T).
 *
 * ### Example scatter a vector of structures, to other processors
 * \snippet VCluster_semantic_unit_tests.hpp Scatter the data from master
 *
 * \tparam T type of sending object
 * \tparam S type of receiving object
 *
 * \param Object to send
 * \param Object to receive
 * \param prc processor involved in the scatter
 * \param sz size of each chunks
 * \param root which processor should scatter the information
 *
 * \return true if the function completed succefully
 *
 */
template<typename T, typename S> bool SScatter(T & send, S & recv, openfpm::vector<size_t> & prc, openfpm::vector<size_t> & sz, size_t root)
{
	// Reset the receive buffer
	reset_recv_buf();

	// If we are on master scatter the information
	if (getProcessUnitID() == root)
	{
		// Prepare the sending buffer
		openfpm::vector<const void *> send_buf;


		openfpm::vector<size_t> sz_byte;
		sz_byte.resize(sz.size());

		size_t ptr = 0;

		for (size_t i = 0; i < sz.size() ; i++)
		{
			send_buf.add((char *)send.getPointer() + sizeof(typename T::value_type)*ptr );
			sz_byte.get(i) = sz.get(i) * sizeof(typename T::value_type);
			ptr += sz.get(i);
		}

		// receive information
		base_info bi(&recv_buf,prc,sz);

		// Send and recv multiple messages
		sendrecvMultipleMessagesNBX(prc.size(),(size_t *)sz_byte.getPointer(),(size_t *)prc.getPointer(),(void **)send_buf.getPointer(),msg_alloc,(void *)&bi);

		// process the received information
		process_receive_buffer<T,S>(recv,NULL);
	}
	else
	{
		// The non-root receive
		openfpm::vector<size_t> send_req;

		// receive information
		base_info bi(&recv_buf,prc,sz);

		// Send and recv multiple messages
		sendrecvMultipleMessagesNBX(send_req.size(),NULL,NULL,NULL,msg_alloc,&bi);

		process_receive_buffer<T,S>(recv,NULL);
	}

	return true;
}


/*! \brief Semantic Send and receive, send the data to processors and receive from the other processors
 *
 * Semantic communication differ from the normal one. They in general
 * follow the following model.
 *
 * SSendRecv(T,S,...,op=add);
 *
 * "SendRecv" indicate the communication pattern, or how the information flow
 * T is the object to send, S is the object that will receive the data.
 * In order to work S must implement the interface S.add(T).
 *
 * ### Example scatter a vector of structures, to other processors
 * \snippet VCluster_semantic_unit_tests.hpp Scatter the data from master
 *
 * \tparam T type of sending object
 * \tparam S type of receiving object
 *
 * \param Object to send
 * \param Object to receive
 * \param prc processor involved in the scatter
 * \param sz size of each chunks
 * \param root which processor should scatter the information
 *
 * \return true if the function completed succefully
 *
 */
template<typename T, typename S> bool SSendRecv(openfpm::vector<T> & send, S & recv, openfpm::vector<size_t> & prc_send, openfpm::vector<size_t> & prc_recv, openfpm::vector<size_t> & sz_recv)
{
	// Reset the receive buffer
	reset_recv_buf();

#ifdef SE_CLASS1

	if (send.size() != prc_send.size())
		std::cerr << __FILE__ << ":" << __LINE__ << " Error, the number of processor involved \"prc.size()\" must match the number of sending buffers \"send.size()\" " << std::endl;

#endif

	// Prepare the sending buffer
	openfpm::vector<const void *> send_buf;

	openfpm::vector<size_t> sz_byte;
	
	size_t tot_size = 0;
	
	for (size_t i = 0; i < send.size() ; i++)
		{
			size_t req = 0;
			
			//Pack requesting		
			pack_unpack_cond<has_max_prop<T, has_value_type<T>::value>::value, T, S>::packingRequest(send.get(i), req, sz_byte);
			tot_size += req;
		}
	
	HeapMemory pmem;
	
	ExtPreAlloc<HeapMemory> & mem = *(new ExtPreAlloc<HeapMemory>(tot_size,pmem));
	mem.incRef();
	
	for (size_t i = 0; i < send.size() ; i++)
	{
		//Packing

		Pack_stat sts;
		
		pack_unpack_cond<has_max_prop<T, has_value_type<T>::value>::value, T, S>::packing(mem, send.get(i), sts, send_buf);

	}

	// receive information
	base_info bi(&recv_buf,prc_recv,sz_recv);

	// Send and recv multiple messages
	sendrecvMultipleMessagesNBX(prc_send.size(),(size_t *)sz_byte.getPointer(),(size_t *)prc_send.getPointer(),(void **)send_buf.getPointer(),msg_alloc,(void *)&bi);

	// process the received information
	process_receive_buffer<T,S>(recv,&sz_recv);

	return true;
}
