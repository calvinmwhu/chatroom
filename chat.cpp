/*A chat room application that supports communication among multiple users;
implemented using reliable, causal ordered, and total ordered multicast protocol.*/
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <algorithm>
#include <dirent.h>
#include <ctype.h>
#include <chrono> 
#include <pthread.h>
#include <thread>         // std::this_thread::sleep_for
#include <queue>          // std::priority_queue
#include <vector>         // std::vector
#include <functional>     // std::greater
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <utility>      // std::pair, std::make_pair
#include <time.h>       /* clock_t, clock, CLOCKS_PER_SEC */
#include <math.h>       /* sqrt */
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <libconfig.h++>

#define MAXBUFLEN 100

using namespace std;
using namespace libconfig;
using namespace boost::algorithm;

struct classcomp {
  bool operator() (const string& lhs, const string& rhs) const
  {
  	return lhs < rhs ;
  }
};

/*struct representing a message*/
struct Mp2_msg
{
	int fromUser;
	int toUser;
	int finalPriority;
	string content;
	bool deliverable;
	vector<int>* vtime;
	Mp2_msg(int f, int t, string s, bool d, vector<int>* v): fromUser(f), toUser(t), finalPriority(-1) , content(s), deliverable(d), vtime(v){} 	
	~Mp2_msg()
	{
		if(vtime) 
			delete vtime;
	}
};

/*struct representing a user
  @ackQueue :  temporily stores all the ack message received by user
  @priorities: 	temporily stores the proposed priorities
  @timestamps: records timestamps of all the users
  @groupInfo: records ip and port of each user
  @causal_holdBackQueue: stores messages not deliverable in causal ordering scheme
  @total_priorityQueue: stores initial messages in total ordering scheme
  @senders: an array of thread id's that are responsible for unicasting message
  @receiver: a pointer to a thread id representing the receiving thread.
  @**lock: mutexes for protecting all the possible shared variables
  @ack_ready: an array of conditional variables for getting signal sent when ackqueue is modified.
*/
typedef struct mp2_user_t
{
	pid_t pid;
	int num_users;
	int userId;
	int meanDelay;
	int dropRate;
	int latest_mid_sent;
	int largest_seq_recv;
	string* ordering;
	string* IPaddr;
	string* port;
	queue<Mp2_msg*>* ackQueue;
	vector<int>* priorities;
	vector<int>* timestamps;
	unordered_map<unsigned int, pair<string, string>* >* groupInfo;
	unordered_set<Mp2_msg*>* causal_holdBackQueue;
	map<string, Mp2_msg*, classcomp>* total_priorityQueue;

	pthread_t* senders;
	pthread_t* receiver;
	pthread_mutex_t* ack_lock;
	pthread_mutex_t* hbq_lock;
	pthread_mutex_t* pri_lock;
	pthread_mutex_t* seq_lock;
	pthread_cond_t* ack_ready;
}mp2_user;

int unicast_send(const char* desIp, const char* desPort, const char* msg);

mp2_user* user;
int TIMEOUT_SEC;

/*for freeing all the memory*/
void clear_user(void)
{
	if(user->senders) delete [] user->senders;
	if(user->receiver) delete user->receiver;
	if(user->ordering) delete user->ordering;
	if(user->IPaddr) delete user->IPaddr;
	if(user->port) delete user->port;
	if(user->timestamps) delete user->timestamps;
	if(user->priorities) delete user->priorities;

	for(int i=0; i<user->num_users; i++)
	{
		delete (*user->groupInfo)[i]; 
	}
	while(!user->ackQueue->empty())	
	{
		Mp2_msg* m = user->ackQueue->front();
		user->ackQueue->pop();
		delete m;
	}	
	delete user->ackQueue;
	delete user->groupInfo;
	pthread_mutex_destroy(user->ack_lock);
	pthread_mutex_destroy(user->hbq_lock);
	pthread_mutex_destroy(user->hbq_lock);
	pthread_mutex_destroy(user->pri_lock);
	pthread_mutex_destroy(user->seq_lock);	

	for(int i=0; i<user->num_users; i++)
	{
		pthread_cond_destroy(&user->ack_ready[i]);		
	}
	delete user->ack_lock;
	delete user->hbq_lock;
	delete user->pri_lock;
	delete user->seq_lock;
	delete [] user->ack_ready;
	delete user->causal_holdBackQueue;
	delete user->total_priorityQueue;

}

/*convert a string to a vector timestamps*/
void get_vector_t(const char* vec,  vector<int > & t )
{
	char* str=NULL;
	asprintf(&str, "%s", vec);
	char * pch;
  	pch = strtok (str," ");
  	while (pch != NULL)
  	{
  		unsigned int x = atoi(pch);
  		t.push_back(x);
    	pch = strtok (NULL, " ");
  	}  
}

void print_timestamp(const vector<int>& t)
{
	for(int i=0; i<t.size(); i++)
	{
		if(i==t.size()-1)
			cout<<t[i];
		else
			cout<<t[i]<<",";
	}
}

/*check if message is deliverable given timestamps info (causal ordering)*/
bool causal_deliverable(const vector<int>& vj, const vector<int>& vi, int j, int i)
{
	for(int k=0; k<vj.size(); k++)
	{
		if(k==j) 
		{
			if(vj[k]==vi[k]+1) continue;
			return false;
		}
		if(vj[k]>vi[k]) return false;
	}
	return true;
}

/*causal ordering: deliver message by printing it to the terminal*/
void causal_deliver_message(int from, const char* msg)
{
	printf("user_%d: %s ", from , msg );
	(*user->timestamps)[from]++;
}

/*Deliver deliverable message from the queue (thread_unsafe)*/
void causal_iterate_queue(void)
{
	auto it = user->causal_holdBackQueue->begin();
	while(it!=user->causal_holdBackQueue->end())
	{
		Mp2_msg* msg = *it;
		if(causal_deliverable(*msg->vtime, *user->timestamps, msg->fromUser, user->userId))
		{
			causal_deliver_message(msg->fromUser, msg->content.c_str());
			user->causal_holdBackQueue->erase(it);
			it=user->causal_holdBackQueue->begin();
			continue;
		}
		++it;
	}
}

/*iterate the queue to find the first one that is deliverable (thread_unsafe)*/
void total_deliver_message(void)
{
	auto it = user->total_priorityQueue->begin() ; 
	while(it!=user->total_priorityQueue->end() && it->second->deliverable)
	{
		cout<<it->second->content;	
		user->total_priorityQueue->erase(it);
		it = user->total_priorityQueue->begin() ; 
	}
}

/*check if a message has already been received*/
bool duplicate_identity_exist(int from, int mid, int& priority)
{
	pthread_mutex_lock(user->pri_lock);
	for(auto it = user->total_priorityQueue->begin(); it!=user->total_priorityQueue->end(); ++it)
	{
		vector<string> key;
		split(key, it->first, is_any_of(" "));
		if(key[1] == to_string(from) && key[2] == to_string(mid) )
		{
			priority = atoi(key[0].c_str());
			pthread_mutex_unlock(user->pri_lock);
			return true;
		}
	}
	pthread_mutex_unlock(user->pri_lock);

	return false;
}

void print_items_in_queue(void)
{
	cout<<"user_"<<user->userId<<"'s priority queue's status: "<<endl;
	for(auto it = user->total_priorityQueue->begin(); it!=user->total_priorityQueue->end(); ++it)
	{
		cout<<it->first<<" ==> " <<(*user->total_priorityQueue)[it->first]->content <<endl;
	}
}



/*
  Function for processing message, according to the following
  MESSAGE FORMAT:
  For CAUSAL ordering: 
	FROM-<id> : TO-<id> : TIMESTAMP-0 0 0 0 : CONTENT-<content>
	FROM-<id> : TO-<id> : ACK
  For TOTAL ordering:
	FROM-<id> : TO-<id> : MID-<mid> : CONTENT-<content> 				(initial message from sender to receiver)
	FROM-<id> : TO-<id> : MID-<mid> : PRIORITY-<X> : CONTENT-<content>  (final message from sender to receiver)
	FROM-<id> : TO-<id> : MID-<mid> : PRIORITY-<X> : ACK 				(receiver's ACK message with priority info to sender)
	FROM-<id> : TO-<id> : ACK 											(sender's ACK message to receiver)    
*/
void process_message(const char* msg )
{
	char* temp = NULL;
	char* content = NULL;
	int from;
	int to;
	int mid=-1;
	int priority=-1;
	asprintf(&temp, "%s", msg);
	char* c = NULL;
	char* vec = NULL;
	char* del;
	bool finalMesg = false;
	char* saveptr = (char*)malloc(256*sizeof(char));

	c = strtok_r(temp, ":", &saveptr);
	while(c !=NULL)
	{
		if(strstr(c,"FROM"))
		{
			del = strstr(c, "-");
			from = atoi(del+1); 
		}
		else if(strstr(c,"TO"))
		{
			del = strstr(c, "-");
			to = atoi(del+1); 
		}
		else if(strstr(c,"MID"))
		{
			del = strstr(c, "-");
			mid = atoi(del+1); 
		}
		else if(strstr(c,"PRIORITY"))
		{
			del = strstr(c, "-");
			priority = atoi(del+1); 
		}
		else if(strstr(c, "TIMESTAMP"))
		{
			del=strstr(c,"-");
			asprintf(&vec, "%s", del+1);
		}
		else if(strstr(c,"CONTENT"))
		{
			del = strstr(c, "-");
			asprintf(&content, "%s", del+1);
			finalMesg = true;
			break;
		}
		else if(strstr(c, "ACK"))
		{
			Mp2_msg* ackmsg = new Mp2_msg(from, user->userId, "ACK", true, NULL);
			pthread_mutex_lock(user->ack_lock);
			user->ackQueue->push(ackmsg);
			pthread_cond_signal(&user->ack_ready[from]);
			pthread_mutex_unlock(user->ack_lock);
			break;
		}
		else
		{
			//no action
		}
		c= strtok_r(NULL, ":", &saveptr);
	}

	char* normal_ack = NULL;
	asprintf(&normal_ack, "FROM-%d : TO-%d : ACK\n", user->userId, from);

	if(*user->ordering == "CAUSAL")
	{
		if(finalMesg)
		{
			unicast_send((*user->groupInfo)[from]->first.c_str(),(*user->groupInfo)[from]->second.c_str(), normal_ack);

			vector<int> temp;
			get_vector_t(vec, temp);
			vector<int>* vtime = new vector<int>(temp);
			Mp2_msg* message = new Mp2_msg(from, user->userId, string(content), true,vtime);
			//first, push message to the causal_holdBackQueue immediately:
			pthread_mutex_lock(user->hbq_lock);
			user->causal_holdBackQueue->insert(message);
			pthread_mutex_unlock(user->hbq_lock);

			//then iterate through the queue to see if any message is deliverable:
			pthread_mutex_lock(user->hbq_lock);
			causal_iterate_queue();
			pthread_mutex_unlock(user->hbq_lock);
		}		
	}
	else  /*total ordering*/
	{
	/*								    
		total_priorityQueue: element type:
		<"x y z", Mp2_msg*>  x->priority, y->from, z->mid
	*/
		if(content)
		{
			char* k = NULL;
			char* ack = NULL;
			Mp2_msg* toMsg = new Mp2_msg(from, to, string(content), false, NULL);
			
			if(mid!=-1 && priority==-1) //initial message from sender!
			{
				//need to mark message as undeliverable and push to queue!	
				int pre_priority = -1;
				if(duplicate_identity_exist(from, mid, pre_priority))
				{
					asprintf(&ack, "FROM-%d : TO-%d : MID-%d : PRIORITY-%d : ACK", user->userId, from, mid, pre_priority);
					unicast_send((*user->groupInfo)[from]->first.c_str(),(*user->groupInfo)[from]->second.c_str(), ack);	
				}
				else
				{
					//send proposed priority to sender together with the acknowlegement
					//and push the undeliverable message to queue!
					asprintf(&k, "%d %d %d", user->largest_seq_recv+1, from, mid);
					string key(k);
					asprintf(&ack, "FROM-%d : TO-%d : MID-%d : PRIORITY-%d : ACK", user->userId, from, mid, user->largest_seq_recv+1);
					unicast_send((*user->groupInfo)[from]->first.c_str(),(*user->groupInfo)[from]->second.c_str(), ack);
					
					pthread_mutex_lock(user->pri_lock);
					(*user->total_priorityQueue)[key] = toMsg;
					pthread_mutex_unlock(user->pri_lock);

				}
			}
			else if(mid!=-1 && priority!=-1 )  //receiver gets final message, need to set it to deliverable!
			{
				unicast_send((*user->groupInfo)[from]->first.c_str(),(*user->groupInfo)[from]->second.c_str(), normal_ack);
				
				pthread_mutex_lock(user->seq_lock);
				user->largest_seq_recv = max(user->largest_seq_recv, priority);
				pthread_mutex_unlock(user->seq_lock);
				
				toMsg->deliverable = true;
				asprintf(&k, "%d %d %d", priority, from, mid);

				string newkey(k);
				pthread_mutex_lock(user->pri_lock);
				for(auto it = user->total_priorityQueue->begin(); it!=user->total_priorityQueue->end(); ++it)
				{
					vector<string> key;
					split(key, it->first, is_any_of(" "));
					if(key[1]== to_string(from) && key[2]==to_string(mid))
					{
						if(newkey != it->first )
						{
							user->total_priorityQueue->erase(it);
							(*user->total_priorityQueue)[newkey] = toMsg;
						}
						else
						{
							(*user->total_priorityQueue)[it->first]->deliverable=true;								
						}
						pthread_mutex_unlock(user->pri_lock);
						break;
					} 
				}
				pthread_mutex_unlock(user->pri_lock);
				
				pthread_mutex_lock(user->pri_lock);
				total_deliver_message();
				pthread_mutex_unlock(user->pri_lock);
			}
			else
			{
				/*nothing*/
			}
			if(k) free(k);
			if(ack) free(ack);
		}
		else
		{
			//sender collects proposed priorities
			if(priority!=-1)
			{
				user->priorities->push_back(priority);
			}
		}
	}

	if(temp) free(temp);
	if(content) free(content);
	if(vec) free(vec);
	if(normal_ack) free(normal_ack);
	del = NULL;
}

static void* mesg_processor_cb(void* arg)
{
	char* buf = (char*)arg;
	long delay = rand()%(2*user->meanDelay); 

	//simulate the delay of messages:
	std::chrono::milliseconds dura( delay );
	std::this_thread::sleep_for( dura );
	
	process_message(buf);
	
	if(buf) free(buf);
	return NULL;
}


//get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*primitive of multicast, keeping reciving messages*/
int unicast_receive(const char* myPort)
{
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
	char buf[MAXBUFLEN];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, myPort, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) 
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("user: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("user: bind");
			continue;
		}
		break;
	}

	if (p == NULL) 
	{
		fprintf(stderr, "user_%d: failed to bind socket\n", user->userId);
		return 2;
	}
	freeaddrinfo(servinfo);

	//use a stl queue to collect all the threads for processing the message 
	queue<pthread_t*> wq;

	/*keep receiving messages from other users*/
	while(1)
	{
		addr_len = sizeof their_addr;
		if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
			(struct sockaddr *)&their_addr, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}
		buf[numbytes] = '\0';

		/***************Spawns multiple threads to process messages***************************************************/
		char* msg = NULL;
		asprintf(&msg, "%s", buf);
		// printf("user_%d receives : %s\n", user->userId , msg );
		pthread_t* td = new pthread_t;
		wq.push(td);
		pthread_create(td, NULL, mesg_processor_cb, (void*)msg);		
		/************************************************************************************************************/

	}
	/****Joins all the child threads, and free the queue****/
	while(!wq.empty())
	{
		pthread_t* temp = wq.front();
		pthread_join(*temp, NULL);
		delete temp;
		wq.pop();
	}
	/*******************************************************/
	close(sockfd);

	return 0;
}

/*
  primitive of multicast, sends a single message
  desIP and desPort denote the destination ip and port number
*/
int unicast_send(const char* desIp, const char* desPort, const char* mesg)
{
	int sockfd;
	char* msg = NULL;
	asprintf(&msg, "%s", mesg);
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(desIp, desPort, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("sender: socket");
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "sender: failed to bind socket\n");
		return 2;
	}

	//simulates drop
	bool dropped = false;
	int dr = rand()%100 ;
	dropped = dr < user->dropRate;

	if(!dropped)
	{
		if ((numbytes = sendto(sockfd, msg, strlen(msg), 0,
				 p->ai_addr, p->ai_addrlen)) == -1) {
			perror("sender: sendto");
			exit(1);
		}
	}
	freeaddrinfo(servinfo);
	close(sockfd);
	return 0;
}


static void* receiver_cb(void* arg)
{
	char* myPort=NULL;
	asprintf(&myPort, "%s", (*user->port).c_str() );
	unicast_receive(myPort);

	if(myPort) free(myPort);
	return NULL;
}

static void* sender_cb(void* arg)
{
	//sender is responsible for freeing the arg!!
	char* toIP=NULL;
	char* toPort = NULL;
	char* content = NULL;
	char* tvStr = NULL;     /*a string representing timestamp, used in causal ordering*/
	char* mid = NULL;		/*a string representing the id of the message; used in total ordering*/
	Mp2_msg* msg = (Mp2_msg*)arg;

	if(*user->ordering == "CAUSAL")
	{
		for(int i=0; i<user->timestamps->size(); i++ )
		{
			if(tvStr)
				asprintf(&tvStr, "%s %d", tvStr, (*user->timestamps)[i]);
			else
				asprintf(&tvStr, "%d", (*user->timestamps)[i]);	
		}
		asprintf(&toIP, "%s", (*user->groupInfo)[msg->toUser]->first.c_str() );
		asprintf(&toPort, "%s", (*user->groupInfo)[msg->toUser]->second.c_str() ) ;
		asprintf(&content, "FROM-%d : TO-%d : TIMESTAMP-%s : CONTENT-%s",
			 msg->fromUser, msg->toUser, tvStr, msg->content.c_str());
	}
	else 
	{
		// cout<<"ready to send "<<msg->content<<endl;
		asprintf(&mid, "%d", user->latest_mid_sent);
		asprintf(&toIP, "%s", (*user->groupInfo)[msg->toUser]->first.c_str() );
		asprintf(&toPort, "%s", (*user->groupInfo)[msg->toUser]->second.c_str() ) ;
		
		if(msg->finalPriority>0)  /*send message with a final determined priority, will be marked as deliverable by receiver*/
			asprintf(&content, "FROM-%d : TO-%d : MID-%s : PRIORITY-%d : CONTENT-%s",
				 msg->fromUser, msg->toUser, mid, msg->finalPriority ,msg->content.c_str() );
		else /*send initial message*/
			asprintf(&content, "FROM-%d : TO-%d : MID-%s : CONTENT-%s", msg->fromUser, msg->toUser, mid, msg->content.c_str() );
	}
	
	struct timeval * now = new struct timeval;
	struct timespec * timeToWait = new struct timespec;		

	while(1)
	{
		Mp2_msg* ack=NULL;
		bool timeout = false;
		unicast_send(toIP, toPort, content);

		/*get argument of timeout delay*/		
		gettimeofday(now, NULL);
		timeToWait->tv_sec  = now->tv_sec + TIMEOUT_SEC;
		timeToWait->tv_nsec = now->tv_usec*1000;
		
		/*use timedwait to block the thread*/
		pthread_mutex_lock(user->ack_lock);
		while(user->ackQueue->size()==0  ||  (ack = user->ackQueue->front())->fromUser != msg->toUser )
		{
			int e=pthread_cond_timedwait(&user->ack_ready[msg->toUser], user->ack_lock, timeToWait);
			if(e!=0) /*time limit has passed, user has to resend the message again*/
			{
				// cout<<"time out"<<endl;
				timeout=true;
				ack = NULL; 
				break;
			}
		}
		if(!timeout && ack ) /*if thread released not due to time_out*/
		{
			delete ack;
			user->ackQueue->pop();
		}
		pthread_mutex_unlock(user->ack_lock);
		
		if(timeout) /*Return to the beginning and resend the message*/
			continue;
		break;	
	}
	// printf("Message sent to user_%d successfully!\n", msg->toUser );
	
	if(toIP) free(toIP);
	if(toPort) free(toPort);
	if(content) free(content);
	if(mid) free(mid);
	if(msg) delete msg;
	delete now;
	delete timeToWait;
	return NULL;
}


/*
Reliable multicast protocol; will be used by
both total ordering and causal ordering protocols;
"final" is the maximum priority determined by the sender.
if the value passed in is -1, it means the sender hasn't determined 
the value, or causal protocol is used(which doesn't require priority).
*/
void R_multicast(const char* msg, int final)
{
	for(int i=0; i<user->num_users; i++)
	{
		if(user->userId == i) continue;
		string str(msg);
		Mp2_msg* message;
		if(*user->ordering == "CAUSAL")
		{
			vector<int>* vtime = new vector<int>(*user->timestamps);
			message = new Mp2_msg(user->userId, i, str, true , vtime);
		}
		else
		{		
			message = new Mp2_msg(user->userId, i, str, false, NULL);	
			if(final>0)
			{
				message->finalPriority = final;
			}
		}
		pthread_create(&user->senders[i], NULL, sender_cb, (void*)message );
	}
	for(int i=0; i<user->num_users; i++)
	{
		if(user->userId == i) continue;
		pthread_join(user->senders[i] ,NULL);
	}
}

/*multicast used by causal ordering*/
void CO_multicast(const char* msg)
{
	(*user->timestamps)[user->userId]++;
	R_multicast(msg, -1);
}

/*multicast used by total ordering*/
void TO_multicast(const char* msg)
{
	user->latest_mid_sent++;	

	//first multicast the message without selected priority
	R_multicast(msg, -1);
	int highest = -1;

	//calculate the highest priority
	for(int i=0; i<user->priorities->size(); i++)
	{
		if((*user->priorities)[i] > highest)
			highest = (*user->priorities)[i] ;
	}
	user->priorities->clear();
	
	//then multicast the final message
	R_multicast(msg, highest);
}

/*
	chatroom simulates the real time chatting software
	user input to the terminal and this function multicast user input
*/
void joinChatRoom(void)
{
	char* buffer;
	size_t len =0;
	pthread_create(user->receiver, NULL, receiver_cb, NULL);
	while(1)
	{
		buffer = NULL;
		//get the message from stdin:
		getline(&buffer, &len, stdin);
	
		if(*user->ordering == "CAUSAL")		
			CO_multicast(buffer);
		else if(*user->ordering == "TOTAL")
			TO_multicast(buffer);
		else
		{
			fprintf(stderr, "Unsupported ordering type!\n");
			exit(1);
		}
		if(buffer) free(buffer);
	}
   	pthread_join(*user->receiver,NULL);
}

/*Get groupinfo from the config file*/
bool init_user_from_file(Config* cf, const char* cfName)
{
	user->ordering = new string;
	user->IPaddr = new string;
	user->port = new string;
	user->groupInfo = new unordered_map<unsigned int, pair<string, string>* > ; 
	cf->lookupValue("chatRoom.ordering", *user->ordering);
	cf->lookupValue("chatRoom.num_of_users", user->num_users);
	user->ackQueue = new queue<Mp2_msg*> ;
	user->priorities = new vector<int> ;
	user->timestamps = new vector<int> ;
	user->causal_holdBackQueue = new unordered_set<Mp2_msg*> ;
	user->total_priorityQueue = new map<string, Mp2_msg*, classcomp > ;
	user->senders = new pthread_t[user->num_users];
	user->receiver = new pthread_t;
	user->ack_lock = new pthread_mutex_t;
	user->hbq_lock = new pthread_mutex_t;
	user->seq_lock = new pthread_mutex_t;
	user->pri_lock = new pthread_mutex_t;
	user->ack_ready = new pthread_cond_t[user->num_users];
	
	pthread_mutex_init(user->ack_lock, NULL);
	pthread_mutex_init(user->hbq_lock, NULL);
	pthread_mutex_init(user->pri_lock, NULL);
	pthread_mutex_init(user->seq_lock, NULL);
	
	for(int i=0; i<user->num_users; i++)
	{
		pthread_cond_init(&user->ack_ready[i], NULL);		
	}

	for(int i=0; i<user->num_users; i++)
	{
		int id;
		string ip, port;	
		bool used;
		user->timestamps->push_back(0);
		char* path = NULL;
		char* pToUsed=NULL;
		asprintf(&pToUsed, "chatRoom.users.[%d].used", i);
		asprintf(&path, "chatRoom.users.[%d]", i);
		Setting & setting = cf->lookup(path);
		
		setting.lookupValue("id", id);
		setting.lookupValue("ip", ip);
		setting.lookupValue("port", port);
		setting.lookupValue("used", used);

		pair<string, string>* p = new pair<string, string>;
		(*p)=make_pair(ip,port);
		(*user->groupInfo)[id]=p;
		if(user->userId != -1) 
		{
			if(pToUsed) free(pToUsed);
			if(path) free(path);
			continue;
		}		
		if(!used)
		{
			user->userId = id;
			*user->IPaddr = ip;
			*user->port = port;
			Setting& s = cf->lookup(pToUsed);
			s = true;
			cf->writeFile(cfName);
			//change used value to true!
		}
		if(pToUsed) free(pToUsed);
		if(path) free(path);
	}

	if(user->userId == -1) return false;
	return true;
}

/*
  set the user field in configuration file to false so that 
  the address is free to be used by incoming users next time
*/
void reset_configFile(Config* cf, const char* cfName)
{
	int num;
	cf->lookupValue("chatRoom.num_of_users", num);
	for(int i=0; i<num; i++)
	{
		char* pToUsed=NULL;
		asprintf(&pToUsed, "chatRoom.users.[%d].used", i);
		Setting& s = cf->lookup(pToUsed);
		if(s) s=false;
		cf->writeFile(cfName);
		if(pToUsed) free(pToUsed);
	}
}

void get_self_ip(void)
{
	struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa ->ifa_addr->sa_family==AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
        } else if (ifa->ifa_addr->sa_family==AF_INET6) { // check it is IP6
            // is a valid IP6 Address
            tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
        } 
    }
    if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);
}


/*Entry of chat*/
int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "usage: %s <configFile> <delayTime> <dropRate> \n", argv[0] );
		exit(1);
	}

	Config* chatRoomInfo = new Config();
	try{
		chatRoomInfo->readFile(argv[1]);		
	}
	catch(const FileIOException &fioex){
		cout<<"Error in reading "<< argv[1]<<endl;
		exit(1);
	}
	catch(const ParseException &pex)
	{
		cout<<"Error in parsing "<< argv[1] <<endl;
		exit(1);
	}
	srand (time(NULL));

	user = (mp2_user*)malloc(sizeof(mp2_user));
	TIMEOUT_SEC = 5;

	user->pid = getpid();
	user->userId=-1;
	user->latest_mid_sent = 0;
	user->largest_seq_recv = 0;
	user->meanDelay = atoi(argv[2]);
	user->dropRate = atoi(argv[3]);

	if(5*user->meanDelay > TIMEOUT_SEC*1000 )
		TIMEOUT_SEC = 5*user->meanDelay/1000 ;

	if(user->dropRate >= 100 && user->dropRate < 0 )
	{
		fprintf(stderr, "%d percent drop rate is impossible ! \n", user->dropRate);
		if(user) free(user);
		exit(1);
	}	
	if(user->meanDelay < 0 )
	{
		fprintf(stderr, "%d meandelay is impossible ! \n", user->meanDelay);
		if(user) free(user);
		exit(1);
	}

	if(!init_user_from_file(chatRoomInfo, argv[1]))
	{
		cout<<"fails to initialize user! All the provided addresses are used!"<<endl;
		exit(1);
	}

	//print user info
	cout<< "user-"<<user->userId<<": " <<"IP-" << *user->IPaddr<<" PORT-"<<*user->port<<" has joined the chat!"<<endl;

	joinChatRoom();
	
	//free all the memory, although maybe never called.
	clear_user();	
	free(user);
	reset_configFile(chatRoomInfo, argv[1]);
	if(chatRoomInfo) delete chatRoomInfo;

	return 0;
}
