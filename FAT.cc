#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

// 文件名的最大长度
#define MAXNAME 8
// 扇区大小
#define SECTOR 512
// 根目录起始扇区
#define DIR 248
// 数据区起始扇区
#define DATA 280
// 每个根目录扇区保存文件信息的数量
#define DIRPERSECT 16
// FAT表扇区的位置
#define FAT1    136
#define FAT2    192
// EOF标志
#define MYEOF -1

using namespace std;

using Byte = unsigned char;
using HalfWord = unsigned short;
using Word = unsigned int;

struct Directory
{
    unsigned char nam[MAXNAME];
    unsigned char	ext[3];
    Byte    atr;
    Byte    r[10];
    HalfWord    tim,day;    
    HalfWord    p0;
    Word    size;   
};

class FAT
{
public:
    FAT(string filename = "disk.vhd")
    {
        fp = fopen(filename.c_str(), "r");
        if(fp == NULL) {
            fclose(fp);
            int data = 0;
            fp = fopen(filename.c_str(), "wb");
            for(int i = 0; i < 4000000; i++)
                fwrite(&data, sizeof(int), 1, fp);
            format();
            fclose(fp);
        }
        fp = fopen(filename.c_str(), "rb+");
    }
    void list(){
        int i,j;
        bool have_file=false;
        Byte buf[SECTOR];
        for (int l=DIR;l<DATA;l++){
            readSect(l,buf);
            for (i=0;i<DIRPERSECT;i++){
                for (j=0;j<8;j++)
                    dir.nam[j]=*(buf+i*32+j);
                for (j=0;j<3;j++)
                    dir.ext[j]=*(buf+i*32+0x8+j);
                dir.p0=read_halfword(buf+i*32+0x1A);
                dir.size=read_Word(buf+i*32+0x1C);

                //该位置没有文件或已删除
                if (dir.nam[0]==0||(Byte)dir.nam[0]==0xE5||dir.p0==0||dir.size==0) continue;

                for (j=0;j<8;j++)
                    if (dir.nam[j]==0x20) break;
                    else
                        printf("%c",dir.nam[j]);

                if (dir.ext[0]!=0x20){
                    printf(".");
                    for (j=0;j<3;j++)
                    if (dir.ext[j]==0x20) break;
                    else
                        printf("%c",dir.ext[j]);
                }

                printf("  Size: %dB\n", dir.size);
                have_file=true;
            }
        }

        if (!have_file)
            printf("[nothing]\n");
        return;
    }
    void deletefile(const char *nam){
        Byte    buf[SECTOR];
        //查找目录
        int index = seekFILE(nam);
        
        if (index>=0) {
            int indexPosition=index % DIRPERSECT * 32;
            readSect(DIR+index/DIRPERSECT,buf);
            buf[indexPosition]=0xE5;
            writeSect(DIR+index/DIRPERSECT,buf);
            printf("1 file removed,\n");
            return;
        }

        printf("File does't exist.");
    }
    // 向磁盘中写入文件
    void writeDisk(const char *nam){
        Byte    buf[SECTOR];
        char x=0;
        FILE *filefp;

        int index = seekFILE(nam);
    
        if (index>=0)
            deleteDir(index);


        if((filefp=fopen(nam,"rb"))==NULL){
            printf("File doesn't exist.\n");
            return;
        }
        
        index = findnewdir();
        if (index<0) {
            printf("disk is full\n");
            return;
        }

        int i,j,t;

        int indexPosition = index % DIRPERSECT * 32;
        
        readSect(DIR+index/DIRPERSECT,buf);

        //写入name到buf
        for (i=0;i<32;i++)
            buf[indexPosition+i]=0;
        for (i=0;i<0x0B;i++)
            buf[indexPosition+i]=0x20;

        j=0;
        while (nam[j]!='.'&&nam[j]!=0) j++;  
        for (i=0;i<j;i++)
            buf[indexPosition+i]=nam[i];

        //写入ext
        t=j;
        while (nam[j]!=0) j++;      
        if (j>t)
            for (i=8;i<8+j-t-1;i++)
                buf[indexPosition+i]=nam[t+1+i-8];

        buf[indexPosition+0x0B]=0x20;

        Word time = get_time();
        buf[indexPosition+0x16]=(Byte)(time>>24)&0xff;
        buf[indexPosition+0x17]=(Byte)(time>>16)&0xff;
        buf[indexPosition+0x18]=(Byte)(time>>8)&0xff;
        buf[indexPosition+0x19]=(Byte)(time&0xff);

        //写入首簇
        HalfWord newSect=findnewsect();        
        if (newSect==0) return;
        write_halfword(buf+indexPosition+0x1A,newSect);
            
        //写入文件长
        fseek(filefp,0L,SEEK_END);
        int size=ftell(filefp);
        write_Word(buf+indexPosition+0x1C,size);
        writeSect(DIR+index/DIRPERSECT,buf);

        //写入文件
        HalfWord p=newSect;
        HalfWord pnext;
        int fptr=0;

        fseek(filefp,0,0);
        while (1){
            if(fptr+512<=size){
                fread(buf,512,1,filefp);
                fptr+=512;
            } else {
                fread(buf,size-fptr,1,filefp);
                fptr=size;
                for (int i=size%512+1;i<512;i++)
                    buf[i]=0;
            }
            writeSect(DATA+p-2,buf);


            if (fptr>=size){
                setFat(p,0xFFFF);
                printf("1 file added.\n");
                break;
            }

            setFat(p,0xFFFF);
            pnext=findnewsect();
            if (pnext==0) 
                return;

            setFat(p,pnext);
            p=pnext;
        }

        fclose(filefp);
    }
    void   format(){
        Byte    buf[SECTOR];

        for (int i=0;i<SECTOR;i++)
            buf[i]=0;

        readSect(DIR,buf);
        for (int i=32;i<SECTOR;i++)
            buf[i]=0;
        for (int i=DIR;i<DATA;i++){
            writeSect(i,buf);
            for (int i=0;i<SECTOR;i++)
            buf[i]=0;
        }

        for (int i=0;i<SECTOR;i++)
            buf[i]=0;

        for (int i=FAT1;i<FAT2;i++){
            // FAT表表头，表示磁盘没有损坏
            if (i==FAT1){
                buf[0]=0xF8;
                buf[1]=0xFF;
                buf[2]=0xFF;
                buf[3]=0xFF;
            }
            writeSect(i,buf);
            for (int i=0;i<SECTOR;i++)
            buf[i]=0;
        }

        for (int i=FAT2;i<DIR;i++){
            if (i==FAT2){
                buf[0]=0xF8;
                buf[1]=0xFF;
                buf[2]=0xFF;
                buf[3]=0xFF;
            }
            writeSect(i,buf);
            for (int i=0;i<SECTOR;i++)
            buf[i]=0;
        }

        printf("Disk Format OK.\n");
        return;
    }
    void readDisk(const char *nam){
        Byte    buf[SECTOR];
        FILE *newfp;

        if (seekFILE(nam)<0){
            printf("File doesn't exist.");
            return;
        }

        newfp=fopen(nam, "wb");

        HalfWord p=dir.p0;
        int fptr=0;

        while (1){
            readSect(DATA+p-2,buf);
            if(fptr+512<=dir.size){
                fwrite(buf,512,1,newfp);
                fptr+=512;
            } 
            else{
                fwrite(buf,dir.size-fptr,1,newfp);
                fptr=dir.size;  
                printf("The file has been read to the current directory.\n");
                break;  
            }
            p=readFAT(p);
        }
        fclose(newfp);
    }
private:
    Directory dir;
    // n is the index of sector
    // read from file
    void readSect(int n, Byte* buf){
        fseek(fp, n * SECTOR, 0);
        fread(buf, 512, 1, fp);
        return;
    }
    // n is the index of sector
    // write to file
    void writeSect(int n, Byte *buf){
        fseek(fp,n * SECTOR,0);
        fwrite(buf,512,1,fp);
        return;
    }
    // 以下为小端规则操作buff
    void write_halfword(Byte* s,HalfWord num){
        Byte first_byte, second_byte;
        first_byte=(num&0xFF00)>>8;
        second_byte=(num&0x00FF);
        s[0]=second_byte;
        s[1]=first_byte;
    }
    void write_Word(Byte* s,Word num){
        Byte byte[4];
        byte[0]=(num&0xFF000000)>>24;
        byte[1]=(num&0x00FF0000)>>16;
        byte[2]=(num&0x0000FF00)>>8;
        byte[3]=(num&0x000000FF);
        for(int i=0;i<4;i++)
            s[i]=byte[3-i];
    }
    HalfWord    read_halfword(Byte* s){
        HalfWord    num=0;
        Byte    first_byte, second_byte;
        first_byte=s[0];
        second_byte=s[1];
        num=second_byte<<8|first_byte;
        return num;
    }

    Word    read_Word(Byte* s){
        Word    num=0;
        Byte    byte[4];
        for (int i=0;i<4;i++)
            byte[i]=s[i];
        num=byte[3]<<24|byte[2]<<16|byte[1]<<8|byte[0];
        return num;
    }
    // 查找目录中文件位置，不存在返回-1
    // 存在返回目录中的位置
    int seekFILE(const char* nam){
        Byte buf[SECTOR];
        char flag;
        int index = -1;
        int i, j, t;
        // 遍历每个根目录扇区
        for (int l = DIR; l < DATA; l++){
            readSect(l,buf);
            // 遍历每个扇区下的每个文件
            for (i = 0; i < DIRPERSECT; i++){
                flag=1;
                for (j=0;j<8;j++)
                    dir.nam[j]=*(buf+i*32+j);
                for (j=0;j<3;j++)
                    dir.ext[j]=*(buf+i*32+0x8+j);
                dir.p0=read_halfword(buf+i*32+0x1A);
                dir.size=read_Word(buf+i*32+0x1C);
                
                // 检查名称与要查找的名称是否一致
                // 后缀名不算
                for (j=0;nam[j]!='.'&&j<8&&nam[j]!=0;j++)
                    if (nam[j]!=dir.nam[j]){
                        flag=0;
                        break;
                    }
                if (dir.nam[j]!=0x20) 
                    flag=0;
                while (nam[j]!='.'&&nam[j]!=0) j++;
                if (nam[j]=='.')    
                    j++;
                // 检查后缀名与文件类型是否一致
                for (t=0;j<strlen(nam);j++,t++)
                    if (nam[j]!=dir.ext[t]){
                        flag=0;
                        break;
                    }
                if (t < 3 && dir.ext[t] != 0x20) 
                    flag=0;
                // 不一致继续查找
                if (!flag) 
                    continue;
                else {    
                    index=(l-DIR)*DIRPERSECT+i;
                    break;
                }
            }
            if (index>=0) 
                break;
        }
        return index;
    }
    // 用于设置FAT表中簇对应的下一个簇号
    void setFat(HalfWord p, HalfWord pnext){
        Byte buf[SECTOR];
        readSect(FAT1 + p / 256, buf);
        write_halfword(buf + p % 256 * 2, pnext);
        writeSect(FAT1 + p / 256, buf);
        readSect(FAT2 + p / 256, buf);
        write_halfword(buf + p % 256 * 2, pnext);
        writeSect(FAT2 + p / 256, buf);
        return;
    }
    // 用于读取FAT表中某个簇的信息
    HalfWord readFAT(HalfWord p){
        Byte buf[SECTOR];
        readSect(FAT1+p/256,buf);
        return read_halfword(buf+p%256*2);
    }
    // 删除某个簇，会把他指向的下一个簇连锁删除
    void deleteSect(HalfWord p){
        HalfWord pnext=readFAT(p);
        setFat(p,0);

        if (pnext==0xFFFF)
            return;
        deleteSect(pnext);
    }
    // 删除根目录下的信息
    void deleteDir(int index){
        HalfWord p0;
        Byte buf[SECTOR];
        readSect(DIR + index / DIRPERSECT, buf);
        p0 = read_halfword(buf + index % DIRPERSECT * 32 + 0x1A);
        // 把FAT表中根目录下的信息清空
        for (int i = index % DIRPERSECT * 32; i < index % DIRPERSECT * 32 + 32;i++)
            buf[i]=0;
        writeSect(DIR+index/DIRPERSECT,buf);
        deleteSect(p0);
    }
    // 在根目录下寻找可以记录的位置
    int     findnewdir(){
        Byte    buf[SECTOR];
        int i,j;
        // 遍历每一个扇区，寻找是否有没有使用的位置
        for (int l=DIR;l<DATA;l++){
            readSect(l,buf);
            for (i=0;i<DIRPERSECT;i++){
                for (j=0;j<8;j++)
                    dir.nam[j]=*(buf+i*32+j);
                for (j=0;j<3;j++)
                    dir.ext[j]=*(buf+i*32+0x8+j);
                dir.p0=read_halfword(buf+i*32+0x1A);
                dir.size=read_Word(buf+i*32+0x1C);
                if (dir.nam[0]==0) 
                    return (l-DIR)*DIRPERSECT+i;
            }
        }
        // 寻找并真正删除删除的文件
        for (int l=DIR;l<DATA;l++){
            readSect(l,buf);
            for (i=0;i<DIRPERSECT;i++){
                for (j=0;j<8;j++)
                    dir.nam[j]=*(buf+i*32+j);
                if (dir.nam[0]==0xE5) 
                    deleteDir((l-DIR)*DIRPERSECT+i);
            }
        }

        // 再次寻找
        for (int l=DIR;l<DATA;l++){
            readSect(l,buf);
            for (i=0;i<DIRPERSECT;i++){
                for (j=0;j<8;j++)
                    dir.nam[j]=*(buf+i*32+j);
                for (j=0;j<3;j++)
                    dir.ext[j]=*(buf+i*32+0x8+j);
                dir.p0=read_halfword(buf+i*32+0x1A);
                dir.size=read_Word(buf+i*32+0x1C);
                if (dir.nam[0]==0) 
                    return (l-DIR)*DIRPERSECT+i;
            }
        }

        printf("can't find new dir.\n");
        return -1;
    }

    // 寻找一个可用扇区
    HalfWord    findnewsect(){
        Byte    buf[SECTOR];
        HalfWord newSect;
        int i,j;
        HalfWord fat;

        // 在FAT表中，搜索可用簇
        for (i=FAT1;i<FAT2;i++){
            readSect(i,buf);
            for(j=0;j<256;j++){
                fat=read_halfword(buf+j*2);
                if (fat==0) 
                    return (i-FAT1)*256+j;
            } 
        }

        // 没有了的话，先真正删除文件，之后再搜索
        for (int l=DIR;l<DATA;l++){
            readSect(l,buf);
            for (i=0;i<DIRPERSECT;i++){
                for (j=0;j<8;j++)
                    dir.nam[j]=*(buf+i*32+j);
                if (dir.nam[0]==0xE5) 
                    deleteDir((l-DIR)*DIRPERSECT+i);
            }
        }

        for (i=FAT1;i<FAT2;i++){
            readSect(i,buf);
            for(j=0;j<256;j++){
                fat=read_halfword(buf+j*2);
                if (fat==0) 
                    return (i-FAT1)*256+j;
            } 
        }

        printf("can't find new sector.\n");
        return 0;
    }
    // 获取时间戳
    Word get_time()
    {
        HalfWord year,month,date,hour,minute,second;
        struct tm *local;
        time_t t;
        t=time(NULL);
        local=localtime(&t);

        year = local->tm_year + 1900;
        month = local->tm_mon+1;
        date = local->tm_mday;
        hour = local->tm_hour;
        minute = local->tm_min;
        second = local->tm_sec;

        return    (Word)((year - 1980) << 25)
                    | (Word)(month << 21)
                    | (Word)(date << 16)
                    | (Word)(hour << 11)
                    | (Word)(minute << 5)
                    | (Word)(second >> 1);
    }
    FILE *fp;
};
int main()
{
    string cmd;
    FAT disk;
    while(1) {
        cout << "root@localhost > ";
        getline(cin, cmd);
        stringstream in(cmd);
        string op;
        in >> op;
        if(op == "dir") 
            disk.list();
        else if(op == "write") {
            string filename;
            in >> filename;
            disk.writeDisk(filename.c_str());
        }
        else if(op == "read") {
            string filename;
            in >> filename;
            disk.readDisk(filename.c_str());
        }
        else if(op == "del") {
            string filename;
            in >> filename;
            disk.deletefile(filename.c_str());
        }
        else if(op == "format")
            disk.format();
        else if(op == "quit") {
            cout << "Bye" << endl;
            break;
        }
        else
            cout << "Invaild Operation" << endl;
    }
    return 0;
}