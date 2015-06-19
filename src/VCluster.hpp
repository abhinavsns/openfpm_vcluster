#ifndef VCLUSTER
#define VCLUSTER

#include <mpi.h>
#include "VCluster_object.hpp"
#include "VCluster_object_array.hpp"
#include "Vector/map_vector.hpp"
#include "MPI_IallreduceW.hpp"
#include <exception>
#include "Vector/map_vector.hpp"

#define MSG_LENGTH 1024
#define MSG_SEND_RECV 1025
#define SEND_SPARSE 1026
#define NONE 1
#define NEED_ALL_SIZE 2

#define MPI_SAFE_CALL(call) {\
	int err = call;\
	if (MPI_SUCCESS != err) {\
		std::cerr << "MPI error: "<< __FILE__ << " " << __LINE__ << "\n";\
		error_handler(err);\
	}\
}

extern size_t n_vcluster;
extern bool global_mpi_init;

///////////////////// Post functions /////////////

template<typename T> void assign(T * ptr1, T * ptr2)
{
	*ptr1 = *ptr2;
};

//////////////////////////////////////////////////

// temporal buffer for reductions
union red
{
	char c;
	unsigned char uc;
	short s;
	unsigned short us;
	int i;
	unsigned int ui;
	float f;
	double d;
};

/*! \brief Virtual Cluster exception
 *
 * This a a class that signal an exception on MPI_WaitAll
 *
 */

class exec_exception: public std::exception
{
  virtual const char* what() const throw()
  {
    return "One or more request has failed or reported an error";
  }
};

/*! \brief This class is virtualize the cluster as a set of processing unit
 *         and communication unit
 *
 * This class virtualize the cluster as a set of processing unit and communication
 * unit. It can execute any vcluster_exe
 *
 */

class Vcluster
{
	// temporal vector used for meta-communication
	// ( or meta-data before the real communication )
	openfpm::vector<size_t> proc_com;

	// vector that contain the scatter map (it is basically an array of one)
	openfpm::vector<int> map_scatter;

	// vector of MPI requests
	openfpm::vector<MPI_Request> req;

	// vector of MPI status
	openfpm::vector<MPI_Status> stat;

	// vector of functions to execute after all the request has been performed
	std::vector<int> post_exe;

	// Object array


	// Single objects

	// number of processes
	int size;
	// actual rank
	int rank;

	// number of processing unit per process
	int numPE = 1;

	/* This buffer is a temporal buffer for reductions
	 *
	 * MPI_Iallreduce does not accept recv and send buffer to be the same
	 * r is used to overcome this problem (is given as second parameter)
	 * after the execution the data is copied back
	 *
	 */
	std::vector<red> r;


	void error_handler(int error_code)
	{
	   char error_string[BUFSIZ];
	   int length_of_error_string, error_class;

	   MPI_Error_class(error_code, &error_class);
	   MPI_Error_string(error_class, error_string, &length_of_error_string);
	   std::cerr << getProcessUnitID() << ": " << error_string;
	   MPI_Error_string(error_code, error_string, &length_of_error_string);
	   std::cerr << getProcessUnitID() << ": " << error_string;
	}


public:

	// Finalize the MPI program
	~Vcluster()
	{
		n_vcluster--;

		// if there are no other vcluster instances finalize
		if (n_vcluster == 0)
			MPI_Finalize();
	}

	//! \brief Virtual cluster constructor
	Vcluster(int *argc, char ***argv)
	{
		n_vcluster++;

		// Check if MPI is already initialized
		if (global_mpi_init == false)
		{
			MPI_Init(argc,argv);
			global_mpi_init = true;
		}

		//! Get the total number of process
		//! and the rank of this process

		MPI_Comm_size(MPI_COMM_WORLD, &size);
		MPI_Comm_rank(MPI_COMM_WORLD, &rank);

#ifdef MEMLEAK_CHECK
			process_v_cl = rank;
#endif

		//! create and fill map scatter with one
		map_scatter.resize(size);

		for (size_t i = 0 ; i < map_scatter.size() ; i++)
		{
			map_scatter.get(i) = 1;
		}
	}

	//! Get the total number of processing units
	size_t getProcessingUnits()
	{
		return size*numPE;
	}

	//! Get the process unit id
	size_t getProcessUnitID()
	{
		return rank;
	}

	/*! \brief Allocate a set of objects
	 *
	 * \tparam obj
	 * \param n number of object
	 *
	 * \return an object representing an array of objects
	 *
	 */
	template <typename obj> Vcluster_object_array<obj> allocate(size_t n)
	{
		// Vcluster object array
		Vcluster_object_array<obj> vo;

		// resize the array
		vo.resize(n);

		// Create the object on memory and return a Vcluster_object_array
		return vo;
	}

	/*! \brief Sum the number across all processors and get the result
	 *
	 * \param num to reduce, input and output
	 *
	 */

	template<typename T> void reduce(T & num)
	{
		// reduce over MPI

		// Create one request
		req.add();

		// reduce
		MPI_IallreduceW<T>::reduce(num,MPI_SUM,req.last());
	}

	/*! \brief Get the maximum number across all processors (or reduction with insinity norm)
	 *
	 * \param num to reduce
	 *
	 */

	template<typename T> void max(T & num)
	{
		// reduce over MPI

		// Create one request
		req.add();

		// reduce
		MPI_IallreduceW<T>::reduce(num,MPI_MAX,req.last());
	}

	// vector of pointers of send buffers
	openfpm::vector<void *> ptr_send;

	// vector of the size of send buffers
	openfpm::vector<size_t> sz_send;

	// sending map
	openfpm::vector<size_t> map;

	// Distributed processor graph
	MPI_Comm proc_comm_graph;

	/*! \brief
	 *
	 * Set the near processor of this processors
	 *
	 */

	openfpm::vector<size_t> NN_proc;

	void setLocality(openfpm::vector<size_t> NN_proc)
	{
		// Number of sources in the graph, and sources processors
		size_t sources = NN_proc.size();
		openfpm::vector<int> src_proc;

		// number of destination in the graph
		size_t dest = NN_proc.size();
		openfpm::vector<int> dest_proc;

		// insert in sources and out sources
		for (size_t i = 0; i < NN_proc.size() ; i++)
		{
			src_proc.add(NN_proc.get(i));
			dest_proc.add(NN_proc.get(i));
			// Copy the vector
			this->NN_proc.get(i) = NN_proc.get(i);
		}

		MPI_Dist_graph_create_adjacent(MPI_COMM_WORLD,sources,&src_proc.get(0),(const int *)MPI_UNWEIGHTED,dest,&dest_proc.get(0),(const int *)MPI_UNWEIGHTED,MPI_INFO_NULL,true,&proc_comm_graph);
	}

	/*! \brief Send and receive multiple messages within local processors
	 *
	 * It send multiple messages and receive
	 * other multiple messages, all the processor must call this
	 * function
	 *
	 * \param prc list of processors with which it should communicate
	 *
	 * \param v vector containing the data to send (it is allowed to have 0 size vector)
	 *
	 * \param msg_alloc This is a call-back with the purpose of allocate space
	 *        for the incoming message and give back a valid pointer, the 6 parameters
	 *        in the call-back are in order:
	 *        1) message size required to receive from i
	 *        2) total message size to receive from all the processors
	 *        3) the total number of processor want to communicate with you
	 *        4) processor id
	 *        5) ri request id (it is an id that goes from 0 to total_p, and is unique
	 *           every time message_alloc is called)
	 *        6) void pointer parameter for additional data to pass to the call-back
	 *
	 * \param opt options, NONE or NEED_ALL_SIZE, with NEED_ALL_SIZE the allocation
	 *        callback will not be called until all the message size will be
	 *        gathered, [usefull for example with you want to allocate one big buffer
	 *        to gather all the messages]
	 *
	 */

	template<typename T> void sendrecvMultipleMessagesNBX(openfpm::vector< size_t > & prc, openfpm::vector< T > & data, void * (* msg_alloc)(size_t,size_t,size_t,size_t,size_t,void *), void * ptr_arg, long int opt=NONE)
	{
		// resize the pointer list
		ptr_send.resize(prc.size());
		sz_send.resize(prc.size());

		for (size_t i = 0 ; i < prc.size() ; i++)
		{
			ptr_send.get(i) = data.get(i).getPointer();
			sz_send.get(i) = data.get(i).size() * sizeof(typename T::value_type);
		}

		sendrecvMultipleMessagesNBX(prc.size(),(size_t *)sz_send.getPointer(),(size_t *)prc.getPointer(),(void **)ptr_send.getPointer(),msg_alloc,ptr_arg,opt);
	}


	/*! \brief Send and receive multiple messages
	 *
	 * It send multiple messages and receive
	 * other multiple messages, all the processor must call this
	 * function
	 *
	 * \param prc list of processors with which it should communicate
	 *
	 * \param nn_prc near processors
	 *
	 * \param v vector containing the data to send
	 *
	 * \param msg_alloc This is a call-back with the purpose of allocate space
	 *        for the incoming message and give back a valid pointer, the 6 parameters
	 *        in the call-back are in order:
	 *        1) message size required to receive from i
	 *        2) total message size to receive from all the processors
	 *        3) the total number of processor want to communicate with you
	 *        4) processor id
	 *        5) ri request id (it is an id that goes from 0 to total_p, and is unique
	 *           every time message_alloc is called)
	 *        6) void pointer parameter for additional data to pass to the call-back
	 *
	 * \param opt options, NONE or NEED_ALL_SIZE, with NEED_ALL_SIZE the allocation
	 *        callback will not be called until all the message size will be
	 *        gathered, [usefull for example with you want to allocate one big buffer
	 *        to gather all the messages]
	 *
	 */

	template<typename T> void sendrecvMultipleMessagesPCX(openfpm::vector< size_t > & prc, openfpm::vector< T > & data, void * (* msg_alloc)(size_t,size_t,size_t,size_t,size_t,void *), void * ptr_arg, long int opt=NONE)
	{
		// resize map with the number of processors
		map.resize(size);

		// reset the sending buffer
		map.fill(0);

		// create sending map
		for (size_t i = 0 ; i < prc.size() ; i++)
		{map.get(prc.get(i)) = 1;}

		// resize the pointer list
		ptr_send.resize(prc.size());
		sz_send.resize(prc.size());

		for (size_t i = 0 ; i < prc.size() ; i++)
		{
			ptr_send.get(i) = data.get(i).getPointer();
			sz_send.get(i) = data.get(i).size() * sizeof(typename T::value_type);
		}

		sendrecvMultipleMessagesPCX(prc.size(),(size_t *)map.getPointer(),(size_t *)sz_send.getPointer(),(size_t *)prc.getPointer(),(void **)ptr_send.getPointer(),msg_alloc,ptr_arg,opt);
	}

	/*! \brief Send and receive multiple messages local
	 *
	 * It send multiple messages to the near processor the and receive
	 * other multiple messages from the, all the processor must call this
	 * function
	 *
	 * \param n_send number of send this processor must do
	 *
	 * \param sz the array contain the size of the message for each processor
	 *        (zeros must be omitted)
	 *
	 *        [Example] for the previous patter 5 10 15 4 mean processor 1
	 *        message size 5 byte, processor 6 message size 10 , ......
	 *
	 * \param prc list of processor with which it should communicate
	 *        [Example] for the previous case should be
	 *        1 6 7 8 (prc and mp contain the same information in different
	 *        format, giving both reduce the computation)
	 *
	 * \param ptr array that contain the messages pointers
	 *
	 * \param msg_alloc This is a call-back with the purpose of allocate space
	 *        for the incoming message and give back a valid pointer, the 6 parameters
	 *        in the call-back are in order:
	 *        1) message size required to receive from i
	 *        2) total message size to receive from all the processors
	 *        3) the total number of processor want to communicate with you
	 *        4) processor id
	 *        5) ri request id (it is an id that goes from 0 to total_p, and is unique
	 *           every time message_alloc is called)
	 *        6) void pointer parameter for additional data to pass to the call-back
	 *
	 * \param opt options, NONE (ignored in this moment)
	 *
	 */

	void sendrecvMultipleMessagesNBX(size_t n_send , size_t sz[], size_t prc[] , void * ptr[], void * (* msg_alloc)(size_t,size_t,size_t,size_t,size_t,void *), void * ptr_arg, long int opt)
	{
		req.clear();
		// Do MPI_Issend

		for (size_t i = 0 ; i < n_send ; i++)
		{
			if (sz[i] != 0)
			{
				req.add();
				MPI_SAFE_CALL(MPI_Issend(ptr[i], sz[i], MPI_BYTE, prc[i], SEND_SPARSE, MPI_COMM_WORLD,&req.last()));
			}
		}

		size_t rid = 0;
		int flag = false;

		bool reached_bar_req = false;
		MPI_Request bar_req;

		// Wait that all the send are acknowledge
		do
		{
			// flag that notify that this processor reach the barrier
			// Barrier request

			MPI_Status stat_t;
			int stat = false;
			MPI_SAFE_CALL(MPI_Iprobe(MPI_ANY_SOURCE,SEND_SPARSE,MPI_COMM_WORLD,&stat,&stat_t));

			// If I have received a message

			if (stat == true)
			{
				// Get the message size
				int msize;
				MPI_SAFE_CALL(MPI_Get_count(&stat_t,MPI_BYTE,&msize));

				// Get the pointer to receive the message
				void * ptr = msg_alloc(msize,0,0,stat_t.MPI_SOURCE,rid,ptr_arg);

				rid++;

				MPI_SAFE_CALL(MPI_Recv(ptr,msize,MPI_BYTE,stat_t.MPI_SOURCE,SEND_SPARSE,MPI_COMM_WORLD,&stat_t));
			}

			// Check the status of all the MPI_issend and call the barrier if finished

			if (reached_bar_req == false)
			{
				int flag = false;
				MPI_SAFE_CALL(MPI_Testall(req.size(),&req.get(0),&flag,MPI_STATUSES_IGNORE));

				// If all send has been completed
				if (flag == true)
				{MPI_SAFE_CALL(MPI_Ibarrier(MPI_COMM_WORLD,&bar_req));reached_bar_req = true;}
			}

			// Check if all processor reach the async barrier
			if (reached_bar_req)
				MPI_Test(&bar_req,&flag,MPI_STATUSES_IGNORE);
		} while (flag == false);

		// Remove the executed request

		req.clear();
		stat.clear();
	}

	/*! \brief Send and receive multiple messages
	 *
	 * It send multiple (to more than one) messages and receive
	 * other multiple messages, all the processor must call this
	 * function
	 *
	 * \param n_send number of send this processor must do
	 *
	 * \param map array containing an array of unsigned chars that
	 *        specify the communication pattern of the processor
	 *
	 *        [Example]   0 1 0 0 0 0 1 1 1 mean that the processor
	 *        communicate with the processor 1 6 7 8
	 *
	 * \param sz the array contain the size of the message for each processor
	 *        (zeros must be omitted)
	 *
	 *        [Example] for the previous patter 5 10 15 4 mean processor 1
	 *        message size 5 byte, processor 6 message size 10 , ......
	 *
	 * \param prc list of processor with which it should communicate
	 *        [Example] for the previous case should be
	 *        1 6 7 8 (prc and mp contain the same information in different
	 *        format, giving both reduce the computation)
	 *
	 * \param ptr array that contain the message (zero lengh must be omitted)
	 *
	 * \param msg_alloc This is a call-back with the purpose of allocate space
	 *        for the incoming message and give back a valid pointer, the 3 parameters
	 *        in the call-back are  , total message to receive, i processor id from witch
	 *        to receive
	 *
	 * \param opt options, NONE or NEED_ALL_SIZE, with NEED_ALL_SIZE the allocation
	 *        callback will not be called until all the message size will be
	 *        gathered, [usefull for example with you want to allocate one big buffer
	 *        to gather all the messages]
	 *
	 */

	void sendrecvMultipleMessagesPCX(size_t n_send, size_t * map, size_t sz[], size_t prc[] , void * ptr[], void * (* msg_alloc)(size_t,size_t,size_t,size_t,size_t,void *), void * ptr_arg, long int opt)
	{
		req.clear();
		req.add();
		stat.add();

		proc_com.resize(1);

		MPI_SAFE_CALL(MPI_Ireduce_scatter(map,&proc_com.get(0),&map_scatter.get(0),MPI_UNSIGNED_LONG,MPI_SUM,MPI_COMM_WORLD,&req.last()));
		MPI_SAFE_CALL(MPI_Waitall(req.size(),&req.get(0),&stat.get(0)));

		// Remove the executed request

		req.clear();
		stat.clear();

		// Allocate the temporal buffer to store the message size for each processor

		size_t n_proc_com = proc_com.get(0);
		proc_com.resize(n_proc_com * 2);

		// queue n_proc_com MPI_Irecv with source ANY_SOURCE to get
		// the message length from each processor and
		//  send the message length to each processor

		for (size_t i = 0 ; i < n_proc_com ; i++)
		{
			req.add();
			MPI_SAFE_CALL(MPI_Irecv(&proc_com.get(i),1,MPI_UNSIGNED_LONG,MPI_ANY_SOURCE,MSG_LENGTH,MPI_COMM_WORLD,&req.last()));
		}

		for (size_t i = 0 ; i < n_send ; i++)
		{
			req.add();
			MPI_SAFE_CALL(MPI_Isend(&sz[i],1,MPI_UNSIGNED_LONG,prc[i],MSG_LENGTH,MPI_COMM_WORLD,&req.last()));
		}

		stat.resize(req.size());
		MPI_SAFE_CALL(MPI_Waitall(req.size(),&req.get(0),&stat.get(0)));

		// Use proc_com to get the processor id that try to communicate

		for (size_t i = 0 ; i < n_proc_com ; i++)
		{
			proc_com.get(n_proc_com+i) = stat.get(i).MPI_SOURCE;
		}

		// Remove the executed request

		req.clear();
		stat.clear();

		// Calculate the total size of the message

		size_t total_msg = 0;

		for (size_t i = 0 ; i < n_proc_com ; i++)
		{
			total_msg += proc_com.get(i);
		}

		// Receive the message

		for (size_t i = 0 ; i < n_proc_com ; i++)
		{
			void * ptr = msg_alloc(proc_com.get(i),total_msg,n_proc_com,proc_com.get(n_proc_com+i),i,ptr_arg);

			req.add();
			MPI_SAFE_CALL(MPI_Irecv(ptr,proc_com.get(i),MPI_BYTE,proc_com.get(i+n_proc_com),MSG_SEND_RECV,MPI_COMM_WORLD,&req.last()));
		}

		// Send all the messages this processor must send

		for (size_t i = 0 ; i < n_send ; i++)
		{
			req.add();
			MPI_SAFE_CALL(MPI_Isend(ptr[i],sz[i],MPI_BYTE,prc[i],MSG_SEND_RECV,MPI_COMM_WORLD,&req.last()));
		}

		stat.resize(req.size());
		MPI_SAFE_CALL(MPI_Waitall(req.size(),&req.get(0),&stat.get(0)));

		// Remove the executed request

		req.clear();
		stat.clear();
	}

	/*! \brief Execute all the request
	 *
	 */
	void execute()
	{
		int err = 0;

		// if req == 0 return
		if (req.size() == 0)
			return;

		// Wait for all the requests
		stat.resize(req.size());
		err = MPI_Waitall(req.size(),&req.get(0),&stat.get(0));

		// MPI error get the message and abort MPI
		if (err != MPI_SUCCESS)
		{
			char * error_msg = NULL;
			int len;
			MPI_Error_string(err,error_msg,&len);

			std::cerr << "Error MPI rank " << rank << ": " << error_msg << "\n";

			MPI_Abort(MPI_COMM_WORLD,1);
		}

		//! Remove executed request and status
		req.clear();
		stat.clear();
	}
};

void init_global_v_cluster(int *argc, char ***argv);

extern Vcluster * global_v_cluster;

#endif

