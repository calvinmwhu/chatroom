CS425 MP2

Group members: Mingwei Hu (mhu9), Martin Liu (liu242)
language: C++ and use of Boost library 

This application simulates a command line chat room and it achieves a reliable, ordering multicast protocol using reliable unicast, total ordering, and causal ordering. This application assumes that all the users' information (ip and port number) is known to the group.

Before compiling the program, please make sure you have libconfig and C++ boost library installed (This program uses libconfig to parse the configuration file). Also make sure you system supports C++11.

After you install these libraries, add the directory containing libconfig to the LD_LIBRARY_PATH by runnning:

 $ sudo ldconfig -v
 $ export LD_LIBRARY_PATH=/usr/local/lib*/ 


Next, you can specify user information in groupInfo.cfg (You're recommended to start with the initial version to see if it works on your system). You can change the number of users (default is 6) and provide additional ip & port for each user,  and also specify the ordering type (default is CAUSAL, you must write it in capitalized letters). However, for testing purpose, this program assumes you are running multiple users on a single machine. So the default ip is localhost and users are differentiated by port numbers.

Next, compile the program by running:

$ make

Then, you should run:

$ make reset

to reset the configuration file (you should run this everytime before running the program). This actually sets all the ip & port addresses to be "un-used" so that next time users comes they have free addresses to choose from. 

Next, open multiple terminals, cd to this directory (where number of terminals open should = number of users specified in the config file).

In each terminal, run the same command (You MUST make sure there as many terminals running the chat at the same time as the number of users specified in the config file):

./chat groupInfo.cfg <meanDelay> <droprate>

And please provide meanDelay (in millisecond) and droprate ( 0 < droprate < 100) as the addtional arguments. Next you can input anything you want to each of these terminal, hit <enter> to send, and when other users receive your message, their should have it printed(delivered) in their terminals. You can thus verify the correctness of total or causal ordering by observing the order in which messages are printed. 

To exit the program, simply run CTRL+C in each terminal.


Please contact us immediately if you encounter any problems in compiling/running the program.
