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
#include <fstream>
#include <sys/mman.h>

using namespace std;

#define INFO_SIGNAL 20
#define PACKET_SIZE 1460
#define PACKET_SPACE 50
#define END_FIRST_ROUND -5
#define END_RETRANSMIT -10


int sockUDP; // UDP socket
struct sockaddr_in serverAddr, clientAddr; // server address for UDP
socklen_t addr_len; // server address length
socklen_t client_addr_len; // client address length
char *receiveBuff; // receive file buffer
char *receiveCount; // receive packet count
int endRetransmit; // end receive retransmit packet
int lostPacketCount; // count lost packet number
int *endFlag; // end signal for retransmission
bool g_start_saving; // start saving packet flag

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
struct packet lostPacketID;


void error(const char *msg) {
    perror(msg);
    exit(0);
}


void createUDPSocket(int &port) {
    sockUDP = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockUDP < 0) {
        error("Create UDP socket");
        exit(1);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    addr_len = sizeof(serverAddr);

    if (::bind(sockUDP, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == -1)
    {
        error("Bind UDP socket");
        exit(1);
    }
    
    cout<<"UDP socket created on port: "<< port <<endl;
}


void recvInfo() {

    // receive file info
    struct fileInfo recvFileInfo;
    socklen_t clientAddr_len = sizeof(clientAddr);
    memset(&recvFileInfo, 0, sizeof(recvFileInfo));

    if (recvfrom(sockUDP, &recvFileInfo, sizeof(recvFileInfo), 0, (struct sockaddr *)&clientAddr, &clientAddr_len) == -1) {
        error("Receive file info");
        exit(1);
    }

    cout<<"File info received by server"<<endl;

    // save the file info
    if (recvFileInfo.packetNum > 0) {
        thisFileInfo.packetNum = recvFileInfo.packetNum;
        thisFileInfo.fileSize = recvFileInfo.fileSize;
    }

    // send respond for receiving file info, ensure the signal get through
    for (int i = 0; i < INFO_SIGNAL; i++) {
        if (sendto(sockUDP, &thisFileInfo, sizeof(thisFileInfo), 0,(const struct sockaddr *)&clientAddr, clientAddr_len) == -1) {
            error("sending file info");
            exit(1);
        }
    }

    cout<<"Send confirmation to client about file info"<<endl;
}


void recvFile() {
    cout<<"Start 1st round receive file"<<endl;

    receiveBuff = (char *)malloc(thisFileInfo.fileSize);
    if(receiveBuff == MAP_FAILED) {
        perror("Failed to allocate memory for receiveBuff");
        exit(1);
    }
    memset(receiveBuff, 0, thisFileInfo.fileSize);

    receiveCount = (char *)malloc(thisFileInfo.packetNum + 1);
    if(receiveCount == MAP_FAILED) {
        perror("Failed to allocate memory for receiveCount");
        exit(1);
    }
    memset(receiveCount, 0, thisFileInfo.packetNum);

    g_start_saving = false;

    while (thisPacket.packetID != END_FIRST_ROUND) {
        memset(&thisPacket, 0, sizeof(thisPacket));

        if (recvfrom(sockUDP, &thisPacket, sizeof(thisPacket), 0, (struct sockaddr *)&clientAddr, &client_addr_len) == -1) {
            error("Receive file info");
            exit(1);
        }
        
        if (!g_start_saving && thisPacket.packetID == thisFileInfo.packetNum) continue;
        g_start_saving = true;

        if (thisPacket.packetID > 0 && thisPacket.packetID <= thisFileInfo.packetNum) {
            memcpy(receiveBuff + ((thisPacket.packetID - 1) * PACKET_SIZE), &thisPacket.packetData, sizeof(thisPacket.packetData));
            memset(receiveCount + thisPacket.packetID, 1, 1);
        }
    }

    cout<<"Finish 1st round receive file"<<endl;
}


void sendLostInfo() {
    char isPacketRecv;
    int retransmitPacketIndex;

    while(1) {
        lostPacketCount = 0;
        retransmitPacketIndex = 0;
        memset(&lostPacketID, 0, sizeof(lostPacketID));

        // find and count the lost packet
        for (int i = 1; i <= thisFileInfo.packetNum; i++) {
            memcpy(&isPacketRecv, receiveCount + i, 1);
            if (isPacketRecv == 0) {
                memcpy(lostPacketID.packetData + 4*retransmitPacketIndex, &i, 4);
                retransmitPacketIndex++;
                lostPacketCount++;
                
                // send lost packet ID when fulfill a packet
                if (lostPacketCount % (PACKET_SIZE / 4) == 0) {
                    lostPacketID.packetID = 0;
                    if(sendto(sockUDP, &lostPacketID, sizeof(lostPacketID), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == -1)
                    {
                        error("Failed to send lost packet info");
                        exit(1);
                    }
                    usleep(PACKET_SPACE);
                    memset(&lostPacketID, 0, sizeof(lostPacketID));
                    retransmitPacketIndex = 0;
                }
            }
        }
        
        // cout << "Number of lost packets: " << lostPacketCount << endl;

        if(lostPacketCount == 0){
            memset(endFlag, 1, 4);
            break;
        }

        // send the remain lost packet ID which not fulfill a whole packet
        if((PACKET_SIZE - (lostPacketCount * 4)) > 0){
            memset(lostPacketID.packetData + (lostPacketCount*4), 0, (PACKET_SIZE - (lostPacketCount * 4)));
        }
        
        if(sendto(sockUDP, &lostPacketID, sizeof(lostPacketID), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == -1)
        {
            error("Failed to send last lost packet ID");
            exit(1);
        }
        usleep(PACKET_SPACE);
    }
}


void *recvRetransmit(void *data) {
    cout << "Start receive retransmission packet" << endl;
    endRetransmit = 0;

    while (endRetransmit == 0) {
        memset(&thisPacket, 0, sizeof(thisPacket));

        if (recvfrom(sockUDP, &thisPacket, sizeof(thisPacket), 0, (struct sockaddr *)&clientAddr, &client_addr_len) == -1) {
            error("Receive file info");
            exit(1);
        }
        
        if (thisPacket.packetID > 0 && thisPacket.packetID <= thisFileInfo.packetNum) {
            memcpy(receiveBuff + ((thisPacket.packetID - 1) * PACKET_SIZE), &thisPacket.packetData, sizeof(thisPacket.packetData));
            memset(receiveCount + thisPacket.packetID, 1, 1);
        }

        memcpy(&endRetransmit, endFlag, 4);
    }

    return NULL;
}


void requestRetransmit() {
    cout << "Start request retransmission lost packet" << endl;

    endFlag = (int *)mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(endFlag, 0, sizeof(int));

    pthread_t thread_id;
    
    pthread_create(&thread_id, NULL, recvRetransmit, NULL);
    
    sendLostInfo();

    //End retransmission by sending packets with packetID = END_RETRANSMIT
    memset(&lostPacketID, 0, sizeof(lostPacketID));
    lostPacketID.packetID = END_RETRANSMIT;
    for(int j = 0; j < INFO_SIGNAL; j++){
        if(sendto(sockUDP, &lostPacketID, sizeof(lostPacketID), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == -1)
        {
            error("Failed to send end retransmit signal");
            exit(1);
        }
        usleep(PACKET_SPACE);
    }

    cout<< "End retransmission lost packet" <<endl;
}


void writeFile(const char* path, const char* buffer, const size_t buffer_size) {
    std::ofstream file;
    try {
        file.open(path, std::ofstream::binary | std::ios::trunc);
    } catch (const std::ofstream::failure& e) {
        std::cerr << "file_write_failed!" << std::endl;
        return;
    }
    file.write(buffer, buffer_size);
    file.close();
    return;
}


int main(int argc, char *argv[]) {
    // ./server <file path> <server port#>
    
    int serverPort = stoi(argv[2]); 
    createUDPSocket(serverPort);

    recvInfo();

    recvFile();

    requestRetransmit();

    close(sockUDP);

    writeFile(argv[1], receiveBuff, thisFileInfo.fileSize);

    cout<<"File transmission end"<<endl;

    return 0;
}

