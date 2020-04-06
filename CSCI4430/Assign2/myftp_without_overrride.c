#include "myftp.h"
#define metadataName  ".metadata"

int sendn(int sd, void *buf, int buf_len)
{

    int n_left = buf_len;
    int n;
    while(n_left > 0){
        if((n = send(sd, buf+(buf_len-n_left), n_left, 0))<0){
            if(errno == EINTR){
                n = 0;
            }else{
                return -1;
            }
        }else if(n == 0){
            return 0;
        }
        n_left -= n;
    }
    return buf_len;
}
int recvn(int sd, void *buf, int buf_len){
    int n_left = buf_len;
    int n;
    while(n_left > 0){
        if((n = recv(sd, buf+(buf_len - n_left), n_left, 0))<0){
            if(errno == EINTR)
                n = 0;
            else
                return -1;
        }else if(n  == 0){
            return 0;
        }
        n_left -= n;
    }
    return buf_len;
}

void *threadFun(void *arg){
    // handle arg
    unsigned char buff[max(MAXLEN,blockSize)];
    struct _threadParam threadParam;
    memcpy(&threadParam, (struct _threadParam *)arg, sizeof(threadParam));
    int client_sd = threadParam.client_sd;
    int n;
    // recv header and save it into buff
    if((n = recvn(client_sd, buff, HEADERLEN)) < 0){
        printf("recv error: %s (ERRNO:%d)\n",strerror(errno), errno);
        exit(0);
    }
    buff[n] = '\0';
    int i;

    struct message_s headerMsg;
    memcpy(&headerMsg, buff, HEADERLEN);
    int sendNum, recvNum;
    
    if(headerMsg.type == (unsigned char)0xA1){
        // get metadata size
        struct stat statbuff;
        int readFileInfoflag;
        if((readFileInfoflag = stat(metadataName, &statbuff))<0){
            printf("error read file info.");
            exit(0);
        }
        int file_size = statbuff.st_size;
        printf("file_size:%d\n", file_size);
        FILE *fd = fopen(metadataName, "rb");

        /****** send header(0XA2, same as HW1 0xFF) *******/
        strcpy(headerMsg.protocol, "myftp");
        headerMsg.type = 0xA2;
        int messageLen = HEADERLEN + file_size;
        headerMsg.length = htonl(messageLen);
        memcpy(buff, &headerMsg, HEADERLEN);
        if((sendNum = (sendn(client_sd, buff, HEADERLEN))<0)){
            printf("send error: %s (ERRNO:%d)\n",strerror(errno), errno);
            exit(0);
        }
        // send file in packet(len <= MAXLEN)
        int remainFileLen = file_size;
        int readLen;
        int nextSize = min(MAXLEN, remainFileLen);
        while((readLen = fread(buff, 1, nextSize, fd))>0){
            if((sendNum = (sendn(client_sd, buff, nextSize))<0)){
                printf("send error: %s (ERRNO:%d)\n",strerror(errno), errno);
                exit(0);
            }
            remainFileLen -= nextSize;
            nextSize = min(MAXLEN, remainFileLen);
        }
        fclose(fd);
        
    }else if(headerMsg.type == (unsigned char)0xB1){

        int fileNameSize = ntohl(headerMsg.length) - HEADERLEN;
        unsigned char *sendFileName = (unsigned char *)malloc(fileNameSize);

        if((n = recvn(client_sd, buff, fileNameSize)) < 0){
            printf("recv error: %s (ERRNO:%d)\n",strerror(errno), errno);
            exit(0);
        }
        memcpy(sendFileName, buff, fileNameSize);
        char *sendFileDir = (char *)malloc(fileNameSize + 5+1);
        strcpy(sendFileDir, "data/");
        strcpy(sendFileDir+5, sendFileName);

        sendFileDir[fileNameSize+6] = '\0';
        int flagExitFile = 0;
        if(access(sendFileDir, F_OK) != 0){
            flagExitFile = 0;
        }else{
            flagExitFile = 1;
        }
    
        struct message_s headerMsgResponse;
        strcpy(headerMsgResponse.protocol, "myftp");
        headerMsgResponse.length = htonl(HEADERLEN);
        
        if(flagExitFile){
            headerMsgResponse.type = (unsigned char)0xB2;
        }else{
            headerMsgResponse.type = (unsigned char)0xB3;
        }
        unsigned char *sendString = (unsigned char *)malloc(HEADERLEN);
        memcpy(sendString, &headerMsgResponse, HEADERLEN);
        if((sendNum = (sendn(client_sd, sendString, HEADERLEN))<0)){
            printf("send error: %s (ERRNO:%d)\n",strerror(errno), errno);
        }
        if(flagExitFile){
            FILE *meta_fd = fopen(".metadata", "rb");
            struct fileInfo_s fileInfo;
            int readFileInfoflag;
            unsigned int fileSize = 0;
            unsigned char idx = 0;
            while((readFileInfoflag = fread(&fileInfo, 1, sizeof(fileInfo), meta_fd))>0){
                if(strcmp(fileInfo.fileName, sendFileName) == 0){
                    idx = fileInfo.idx;
                    fileSize = fileInfo.fileSize;
                    break;
                }
            }
            printf("sendFileDir:%s\n", sendFileDir);
            printf("idx:%d\n", idx);
            printf("fileSize:%d\n", fileSize);
            fclose(meta_fd);
            FILE *fd = fopen(sendFileDir, "rb");
            strcpy(headerMsg.protocol, "myftp");
            headerMsg.type = 0xFF;
            headerMsg.idx = idx;
            unsigned int lenHeaderMsg = HEADERLEN + fileSize;
            headerMsg.length = htonl(lenHeaderMsg);
            memcpy(buff, &headerMsg, HEADERLEN);
            unsigned int remainFileLen = fileSize;
            unsigned int readLen;
            if((sendNum = sendn(client_sd, buff, HEADERLEN))<0){
                printf("send error: %s (ERRNO:%d)\n",strerror(errno), errno);
                exit(0);
            }
            int nextSize=  min(blockSize, remainFileLen);
            while((readLen = fread(buff, 1, nextSize, fd))>0){
                nextSize=  min(blockSize, remainFileLen);
                if((sendNum = (sendn(client_sd, buff, nextSize))<0)){
                    printf("send error: %s (ERRNO:%d)\n",strerror(errno), errno);
                    exit(0);
                }
                remainFileLen -= nextSize;
            }
            fclose(fd);
        }

    }else if(headerMsg.type == (unsigned char)0xC1){
        int fileNameSize = ntohl(headerMsg.length) - 11;
        unsigned char *recvFileName = (unsigned char *)malloc(fileNameSize);

        struct fileInfo_s fileInfo;
        memset(fileInfo.fileName, 0, 1024); //initialize the filename
        
        
        if((n = recvn(client_sd, buff, fileNameSize)) < 0){
            printf("recv error: %s (ERRNO:%d)\n",strerror(errno), errno);
            exit(0);
        }
        memcpy(recvFileName, buff, fileNameSize);
        memcpy(fileInfo.fileName, buff, fileNameSize);
        // copy the filename to fileInfo

        struct message_s headerMsg_response;
        strcpy(headerMsg_response.protocol, "myftp");
        headerMsg_response.type = (unsigned char)0xC2;
        headerMsg_response.length = htonl(11);
        
        unsigned char *sendString = (unsigned char *)malloc(11);
        memcpy(sendString, &headerMsg_response, 11);
        if((sendNum = (sendn(client_sd, sendString, 11))<0)){
            printf("send error: %s (ERRNO:%d)\n",strerror(errno), errno);
        }
        if((recvNum = recvn(client_sd, buff, 11)) < 0){
            printf("recv error: %s (ERRNO:%d)\n",strerror(errno), errno);
            exit(0);
        }
        memcpy(&headerMsg, buff, 11);
        if(headerMsg.type == (unsigned char)0xFF){
            char *recvFileDir = (char *)malloc(fileNameSize + 5+1);
            strcpy(recvFileDir, "data/");
            strcpy(recvFileDir+5, recvFileName);
            recvFileDir[fileNameSize+6] = '\0';
            
            FILE *fd = fopen(recvFileDir, "wb");
            FILE *metafd = fopen(".metadata", "ab");// add the metadata fd

            int fileSize;
            int org_file_size = ntohl(headerMsg.length) - 11;
            printf("Received file: %s, size: %d\n", recvFileName, org_file_size);
            
            if(org_file_size % (K*blockSize) == 0){
                fileSize = org_file_size / K;
            }else{
                fileSize = (org_file_size + (K*blockSize - org_file_size % (K*blockSize))) / K; 
            }
            
            printf("Save size: %d\n",fileSize );
            fileInfo.fileSize = org_file_size;
            fileInfo.idx = headerMsg.idx;
            memcpy(buff, &fileInfo, FILEINFOSIZE);
            // write the fileinfo to metadata
            if((fwrite(buff, 1, FILEINFOSIZE, metafd)) < 0){ 
                printf("Metadata write error for file: %s\n", fileInfo.fileName);
            }
            fclose(metafd);

            int remainSize = fileSize;
            int nextSize;
            while(remainSize>0){
                nextSize = min(remainSize, MAXLEN);
                if((recvNum = recvn(client_sd, buff, nextSize)) < 0){
                    printf("recv error: %s (ERRNO:%d)\n",strerror(errno), errno);
                    exit(0);
                }
                fwrite(buff, 1, nextSize, fd);
                remainSize -= nextSize;
            }
            fclose(fd);
        }
    }
    close(client_sd);
    int threadIdx = threadParam.threadClientIdx;
    //printf("finish idx:%d\n", threadIdx);
    fflush(stdout);
    threadClient[threadIdx].available = 1;
}

void sort(int *input, int len){
    int i, j;
    for(i = 0;i < len - 1;i++){
        int min = input[i];
        int minIdx = i;
        for(j = i + 1;j < len;j++){
            if(input[j] < min){
                min = input[j];
                minIdx = j;
            }
        }
        int tmp = input[i];
        input[i] = input[minIdx];
        input[minIdx] = tmp;
    }
}

int in(int *array, int val, int len){
    int j;
    int flagIn = 0;
    for(j = 0;j < len;j++){
        if(array[j] == val){
            flagIn = 1;
            break;
        }
    }
    return flagIn;
}
uint8_t* decodeData(int n, int k, int *workNodes, unsigned char **data, unsigned char *result){
    int i;
    //printf("\n");
    sort(workNodes, K);

    errorMatrix = (uint8_t *)malloc(sizeof(uint8_t) * (k*k));
    invertMatrix = (uint8_t *)malloc(sizeof(uint8_t) * (k*k));
    encodeMatrix = (uint8_t *)malloc(sizeof(uint8_t) * (n*k));

    gf_gen_rs_matrix(encodeMatrix, n, k);

    unsigned char **dest = (unsigned char **)malloc(sizeof(unsigned char *)*n);
    for(i = 0;i < n-k;i++){
        dest[i] = (unsigned char *)malloc(sizeof(unsigned char )*blockSize);
    }
    
    for(i = 0; i < k;i++){
        int r  = workNodes[i];
        int j;
        for(j = 0; j < k;j++){
            errorMatrix[k*i + j] = encodeMatrix[k*r+j];
        }
    }
    

    gf_invert_matrix(errorMatrix, invertMatrix, k);
    uint8_t *decodeMatrix = (uint8_t *)malloc(sizeof(uint8_t) * k*k);
    int missCount = 0;
    int *missIdx = (int *)malloc(sizeof(int) * k);
    
    for(i = 0;i < k;i++){
        if(in(workNodes, i, k) == 0){
            missIdx[missCount] = i;
            missCount++;
        }
    }
    for(i = 0 ;i < k;i++){
        int j;
        for(j = 0;j < k;j++){
            if(i < missCount){
                decodeMatrix[i*k+j] = invertMatrix[missIdx[i]*k+j];
            }else{
                decodeMatrix[i*k+j] = 0;
            }
        }
    }


    uint8_t *table = (uint8_t *)malloc(sizeof(uint8_t)*32*k*(n-k));
    ec_init_tables(k, n-k, decodeMatrix, table);
    ec_encode_data(blockSize, k, n-k,table, data, dest);
    
    unsigned char **retrievedData = (unsigned char **)malloc(sizeof(unsigned char *)*k);
    int srcCount = 0;
    int destCount = 0;
    for(i = 0;i < k;i++){
        if(!in(workNodes, i, k)){
            retrievedData[i] = dest[destCount];
            destCount++;
        }else{
            retrievedData[i] = data[srcCount];
            srcCount++;
        }
    }
    for(i = 0;i < k;i++){
        int j;
        for(j = 0;j < blockSize;j++){
            result[i*blockSize+j] = retrievedData[i][j];
        }
    }
}
