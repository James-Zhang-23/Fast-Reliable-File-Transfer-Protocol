#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <new>
#include <fstream>
#include <sys/mman.h>
#include <sys/time.h>

using namespace std;

#define INFO_SIGNAL 20
#define PACKET_SIZE 1460
#define PACKET_SPACE 30
#define END_FIRST_ROUND -5
#define END_RETRANSMIT -10


int sockUDP; // UDP socket
struct sockaddr_in serverAddr; // server address for UDP
socklen_t addr_len; // address length
char *buff; // file buff
char *retransmitInfo; // packet ID need to retransmit
int endRetransmit; // end receive retransmit packet
int *endFlag; // end signal for retransmission 


struct fileInfo{
    int packetNum;
    int fileSize;
};
struct fileInfo thisFileInfo;

struct packet{
    int packetID;
    char packetData[PACKET_SIZE];
};
struct packet thisPacket;
struct packet retransmitPacket;

void error(const char *msg) {
    perror(msg);
    exit(0);
}


void readFile(const char* path, char* &buff_p) {
    // open file
    FILE *read_file;
    if((read_file = fopen(path, "rb")) == NULL){
        printf("Error opening specified file.\n");
        exit(1);
    }
    fseek(read_file, 0, SEEK_END);
    thisFileInfo.fileSize = (int)ftell(read_file);
    fseek(read_file, 0, SEEK_SET);
    thisFileInfo.packetNum = thisFileInfo.fileSize / PACKET_SIZE;
    if((thisFileInfo.fileSize % PACKET_SIZE) != 0) thisFileInfo.packetNum++;

    //map file into buffer
    buff = (char *)malloc(thisFileInfo.fileSize + 1);
    memset(buff, 0, thisFileInfo.fileSize + 1);
    fread(buff, 1, thisFileInfo.fileSize, read_file);
    fclose(read_file);

    cout<<"Total file size = "<< thisFileInfo.fileSize << endl;
    cout<<"Total packet number = "<< thisFileInfo.packetNum <<endl;
}


void createUDPSocket(const char* address, int &port) {
    sockUDP = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockUDP < 0) {
        error("Create UDP socket");
        exit(1);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(address);
    serverAddr.sin_port = htons(port);
    addr_len = sizeof(serverAddr);

    cout<<"UDP socket created"<<endl;
}


void sendInfo() {
    
    // send file info, ensure the signal get through
    for (int i = 0; i < INFO_SIGNAL; i++) {
        if (sendto(sockUDP, &thisFileInfo, sizeof(thisFileInfo), 0,(const struct sockaddr *)&serverAddr, addr_len) == -1) {
            error("sending file info");
            exit(1);
        }
        usleep(PACKET_SPACE);
    }

    cout << "Sent file info to server"<< endl;

    // wait for file info received respond
    struct fileInfo recvFileInfoSignal;
    while (recvFileInfoSignal.packetNum != thisFileInfo.packetNum) {
        if (recvfrom(sockUDP, &recvFileInfoSignal, sizeof(recvFileInfoSignal), 0, NULL, NULL) == -1) {
            error("receive file info signal");
            exit(1);
        }
    }

    cout<<"File info received by server"<<endl;

}


void sendFile() {
    cout<<"Start 1st round send file"<<endl;
    int sentCount = 0;
    for (int i = 1; i <= thisFileInfo.packetNum; i++) {
        memset(&thisPacket, 0, sizeof(thisPacket));
        memcpy(thisPacket.packetData, buff + sentCount, PACKET_SIZE);
        thisPacket.packetID = i;

        if (sendto(sockUDP, &thisPacket, sizeof(thisPacket), 0,(const struct sockaddr *)&serverAddr, addr_len) == -1) {
            cout<<"packet: " << thisPacket.packetID <<"send error"<<endl;
        }
        
        sentCount += PACKET_SIZE;
        usleep(PACKET_SPACE); // wait 50us prevent overflow
    }
    
    cout<<"Finish 1st round send file"<<endl;

    for (int i = 0; i <= INFO_SIGNAL; i++) {
        memset(&thisPacket, 0, sizeof(thisPacket));
        thisPacket.packetID = END_FIRST_ROUND;

        if (sendto(sockUDP, &thisPacket, sizeof(thisPacket), 0,(const struct sockaddr *)&serverAddr, addr_len) == -1) {
            cout<<"End first round signal error"<<endl;
        }

        usleep(PACKET_SPACE);
    }
}

void recvLostInfo() {
    while(1){
        memset(&thisPacket, 0, sizeof(thisPacket));
        if (recvfrom(sockUDP, &thisPacket, sizeof(thisPacket), 0, NULL, NULL) == -1)
        {
            error("Fail to receive lost packet ID");
            exit(1);
        }
        if(thisPacket.packetID == END_RETRANSMIT){
            memset(endFlag, 1, 4);
            break;
        }
        memcpy(retransmitInfo, thisPacket.packetData, PACKET_SIZE);
    }
}

void *sendRetransmit(void *data) {
    endRetransmit = 0;
    int retransmitID;

    while(endRetransmit == 0) {
        for (int i = 0; i < PACKET_SIZE; i++) {
            memcpy(&retransmitID, retransmitInfo + i, 4);
            if ((retransmitID > 0) && (retransmitID <= thisFileInfo.packetNum)) {
                memset(&retransmitPacket, 0, sizeof(retransmitPacket));
                memcpy(retransmitPacket.packetData, buff + (retransmitID - 1)*PACKET_SIZE, PACKET_SIZE);
                retransmitPacket.packetID = retransmitID;

                if(sendto(sockUDP, &retransmitPacket, sizeof(retransmitPacket), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == -1)
                {
                    error("Failed to send lost packet");
                    exit(1);
                }

                // cout<<"Retransmit packet id: "<< retransmitID <<endl;
                usleep(PACKET_SPACE);
            }

        }
        memcpy(&endRetransmit, endFlag, 4);
    }
    pthread_exit(NULL);
}

void retransmit() {
    cout << "Start retransmission lost packet" << endl;

    retransmitInfo = (char *)mmap(NULL, PACKET_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 1, 0);
    memset(retransmitInfo, 0, PACKET_SIZE);
    endFlag = (int *)mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(endFlag, 0, 4);

    pthread_t thread_id;

    pthread_create(&thread_id, NULL, sendRetransmit, NULL);

    recvLostInfo();

    usleep(500);
}


int main(int argc, char *argv[]) {
    // ./client <file path> <server address> <server port#>

    // timer start
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    double start = (tv1.tv_sec) + (tv1.tv_usec)*1e-6;

    readFile(argv[1], buff);
    
    int serverPort = stoi(argv[3]); 
    createUDPSocket(argv[2], serverPort);

    sendInfo();

    sendFile();

    retransmit();

    close(sockUDP);

    // timer end
    gettimeofday(&tv1, NULL);
    double end = (tv1.tv_sec) + (tv1.tv_usec)*1e-6;
    double time = end - start;

    double fileSizeMb = (thisFileInfo.fileSize / 1000000.0) * 8;

    double throughput = fileSizeMb / time;

    cout<<"File transmission end"<<endl;

    cout<< "Transfer file size = " << fileSizeMb << " Mbits" << endl;
    cout<<"Total file transmission time = "<<time<<"sec"<<endl;
    cout<< "Throughput = "<< throughput <<" Mbits/s"<< endl;

    return 0;
}