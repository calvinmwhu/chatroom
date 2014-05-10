TARGET = chat
LDLIB = -L /usr/local/lib/libconfig++.so.9

all: $(TARGET) resetGroupInfo

$(TARGET): $(TARGET).cpp
	g++ -std=c++11 `pkg-config --cflags libconfig++` -g $(TARGET).cpp -o $(TARGET) -lpthread `pkg-config --libs libconfig++`	

resetGroupInfo: resetConfigFile.cpp
	g++ -std=c++11 `pkg-config --cflags libconfig++` -g resetConfigFile.cpp -o resetGroupInfo `pkg-config --libs libconfig++`

reset:
	./resetGroupInfo groupInfo.cfg

chat1:
	./chat groupInfo.cfg 2000 5

commit:
	git add -A
	git commit -m "mp2"
	git push -u origin master

clean:
	rm -rf *o $(TARGET) resetGroupInfo

