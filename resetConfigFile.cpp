#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libconfig.h++>


using namespace std;
using namespace libconfig;


void reset_configFile(Config* cf, const char* cfName)
{
    const Setting& chatRoom = cf->getRoot();
    const Setting& users = chatRoom["chatRoom"]["users"];
    int num = 0;
    try
  	{
	    num = users.getLength();
    	cout<<"There are " <<num <<" avaiable addresses that can be used!"<<endl;
  	}
  	catch(const SettingNotFoundException &nfex)
  	{
    	cout<<"No setting is found!"<<endl;
    	return ;
  	}
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


int main(int argc, char** argv)
{
	if(argc!=2)
	{
		fprintf(stderr, "usage: %s <filename>\n", argv[0]);
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

	reset_configFile(chatRoomInfo, argv[1]);

	delete chatRoomInfo;


	return 0;
}