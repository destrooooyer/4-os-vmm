#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "vmm.h"

/* 页表 */
PageTableItem pageTable[PAGE_SUM];
/* 实存空间 */
BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
FILE *ptr_auxMem;
/* 物理块使用标识 */
BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
Ptr_MemoryAccessRequest ptr_memAccReq;



/* 初始化环境 */
void do_init()
{
	int i, j;
	srandom(time(NULL));
/********************************************************************************/
	//初始化辅存,不然程序运行不了
	unsigned char c;
	for(i=0;i<64*4;i++){
		c = random() % 0xFFu;
		fprintf(ptr_auxMem, "%c", c);
	}
/********************************************************************************/	



	//初始化页表
	for (i = 0; i < PAGE_SUM; i++)
	{
		pageTable[i].pageNum = i;
		pageTable[i].filled = FALSE;
		pageTable[i].edited = FALSE;
		pageTable[i].count = 0;
		/* 使用随机数设置该页的保护类型 */
		switch (random() % 7)
		{
			case 0:
			{
				pageTable[i].proType = READABLE;
				break;
			}
			case 1:
			{
				pageTable[i].proType = WRITABLE;
				break;
			}
			case 2:
			{
				pageTable[i].proType = EXECUTABLE;
				break;
			}
			case 3:
			{
				pageTable[i].proType = READABLE | WRITABLE;
				break;
			}
			case 4:
			{
				pageTable[i].proType = READABLE | EXECUTABLE;
				break;
			}
			case 5:
			{
				pageTable[i].proType = WRITABLE | EXECUTABLE;
				break;
			}
			case 6:
			{
				pageTable[i].proType = READABLE | WRITABLE | EXECUTABLE;
				break;
			}
			default:
				break;
		}
		/* 设置该页对应的辅存地址 */
/***********************************************************************************/
		//64个页表项对应辅存的64页,乘2是什么意思？？？
		//pageTable[i].auxAddr = i * PAGE_SIZE * 2;
		pageTable[i].auxAddr = i * PAGE_SIZE;
/***********************************************************************************/	
	}
	for (j = 0; j < BLOCK_SUM; j++)
	{
		/* 随机选择一些物理块进行页面装入 */
		if (random() % 2 == 0)
		{
			do_page_in(&pageTable[j], j);
			pageTable[j].blockNum = j;
			pageTable[j].filled = TRUE;
			blockStatus[j] = TRUE;
		}
		else
			blockStatus[j] = FALSE;
	}
}


/* 响应请求 */
void do_response()
{
	Ptr_PageTableItem ptr_pageTabIt;
	unsigned int pageNum, offAddr;
	unsigned int actAddr;
	
	/* 检查地址是否越界 */
	if (ptr_memAccReq->virAddr < 0 || ptr_memAccReq->virAddr >= VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_OVER_BOUNDARY);
		return;
	}
	
	/* 计算页号和页内偏移值 */
	pageNum = ptr_memAccReq->virAddr / PAGE_SIZE;
	offAddr = ptr_memAccReq->virAddr % PAGE_SIZE;
	printf("页号为：%u\t页内偏移为：%u\n", pageNum, offAddr);

	/* 获取对应页表项 */
	ptr_pageTabIt = &pageTable[pageNum];
	
	/* 根据特征位决定是否产生缺页中断 */
	//当前页表为空
	if (!ptr_pageTabIt->filled)
	{
		do_page_fault(ptr_pageTabIt);
	}
	
	actAddr = ptr_pageTabIt->blockNum * PAGE_SIZE + offAddr;
	printf("实地址为：%u\n", actAddr);
	
	/* 检查页面访问权限并处理访存请求 */
	switch (ptr_memAccReq->reqType)
	{
		case REQUEST_READ: //读请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & READABLE)) //页面不可读
			{
				do_error(ERROR_READ_DENY);
				return;
			}
			/* 读取实存中的内容 */
			printf("读操作成功：值为%02X\n", actMem[actAddr]);
			break;
		}
		case REQUEST_WRITE: //写请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & WRITABLE)) //页面不可写
			{
				do_error(ERROR_WRITE_DENY);	
				return;
			}
			/* 向实存中写入请求的内容 */
			actMem[actAddr] = ptr_memAccReq->value;
			ptr_pageTabIt->edited = TRUE;			
			printf("写操作成功\n");
			break;
		}
		case REQUEST_EXECUTE: //执行请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & EXECUTABLE)) //页面不可执行
			{
				do_error(ERROR_EXECUTE_DENY);
				return;
			}			
			printf("执行成功\n");
			break;
		}
		default: //非法请求类型
		{	
			do_error(ERROR_INVALID_REQUEST);
			return;
		}
	}
}

/* 处理缺页中断 */
void do_page_fault(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i;
	printf("产生缺页中断，开始进行调页...\n");
	//先找没有写进页表中的物理块,没有的话进行调度
	for (i = 0; i < BLOCK_SUM; i++)
	{
		if (!blockStatus[i])
		{
			/* 读辅存内容，写入到实存 */
			do_page_in(ptr_pageTabIt, i);
			
			/* 更新页表内容 */
			ptr_pageTabIt->blockNum = i;
			ptr_pageTabIt->filled = TRUE;
			ptr_pageTabIt->edited = FALSE;
			ptr_pageTabIt->count = 0;
			
			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	do_LFU(ptr_pageTabIt);
}

/* 根据LFU算法进行页面替换 */
void do_LFU(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i, min, page;
	printf("没有空闲物理块，开始进行LFU页面替换...\n");
/*******************************************************************************/
	//算法似乎有问题，应该从装入了物理块的页中选择最少使用次数的
/*******************************************************************************/	

	for (i = 0, min = 0xFFFFFFFF, page = 0; i < PAGE_SUM; i++)
	{
		//if (pageTable[i].count < min)
/*******************************************************************************/
		if (pageTable[i].filled && pageTable[i].count < min)
/*******************************************************************************/
		{
			min = pageTable[i].count;
			page = i;
		}
	}
	printf("选择第%u页进行替换\n", page);
	//该页面对应的物理块修改了
	if (pageTable[page].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&pageTable[page]);
	}
	pageTable[page].filled = FALSE;
	pageTable[page].count = 0;


	/* 读辅存内容，写入到实存 */
	do_page_in(ptr_pageTabIt, pageTable[page].blockNum);
	
	/* 更新页表内容 */
	ptr_pageTabIt->blockNum = pageTable[page].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;
	printf("页面替换成功\n");
}

/* 将辅存内容写入实存 */
void do_page_in(Ptr_PageTableItem ptr_pageTabIt, unsigned int blockNum)
{
	unsigned int readNum;

	//打开辅存文件ptr_auxMem,从文件开头(SEEK_SET)处偏移ptr_pageTabIt->auxAddr(该页表在辅存中的位置)开始读取数据
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}

	//从之前打开的ptr_auxMem文件流中读取数据,写入到实存中指定块的位置(actMem + blockNum * PAGE_SIZE)
	//读写字节的大小(sizeof(BYTE))           读写4个字节(PAGE_SIZE)

	//初始化时并没有设置辅存中的数据，所以辅存为空，读取会失败
	if ((readNum = fread(actMem + blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: blockNum=%u\treadNum=%u\n", blockNum, readNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("调页成功：辅存地址%u-->>物理块%u\n", ptr_pageTabIt->auxAddr, blockNum);

}

/* 将被替换页面的内容写回辅存 */
void do_page_out(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int writeNum;
	//同上，从辅存中相应页面的地址开始读写
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}

	//fwrite()与fread()的区别是,fwrite()执行完之后必须关闭流fclose(),而fread()不关闭会将流移动到上次读写结束的地址
	if ((writeNum = fwrite(actMem + ptr_pageTabIt->blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: writeNum=%u\n", writeNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_WRITE_FAILED);
		exit(1);
	}
	printf("写回成功：物理块%u-->>辅存地址%03X\n", ptr_pageTabIt->auxAddr, ptr_pageTabIt->blockNum);

/********************************************************************************************************/
	//fclose(ptr_auxMem);   main函数里面关闭了
/********************************************************************************************************/	

}

/* 错误处理 */
void do_error(ERROR_CODE code)
{
	switch (code)
	{
		case ERROR_READ_DENY:
		{
			printf("访存失败：该地址内容不可读\n");
			break;
		}
		case ERROR_WRITE_DENY:
		{
			printf("访存失败：该地址内容不可写\n");
			break;
		}
		case ERROR_EXECUTE_DENY:
		{
			printf("访存失败：该地址内容不可执行\n");
			break;
		}		
		case ERROR_INVALID_REQUEST:
		{
			printf("访存失败：非法访存请求\n");
			break;
		}
		case ERROR_OVER_BOUNDARY:
		{
			printf("访存失败：地址越界\n");
			break;
		}
		case ERROR_FILE_OPEN_FAILED:
		{
			printf("系统错误：打开文件失败\n");
			break;
		}
		case ERROR_FILE_CLOSE_FAILED:
		{
			printf("系统错误：关闭文件失败\n");
			break;
		}
		case ERROR_FILE_SEEK_FAILED:
		{
			printf("系统错误：文件指针定位失败\n");
			break;
		}
		case ERROR_FILE_READ_FAILED:
		{
			printf("系统错误：读取文件失败\n");
			break;
		}
		case ERROR_FILE_WRITE_FAILED:
		{
			printf("系统错误：写入文件失败\n");
			break;
		}
		default:
		{
			printf("未知错误：没有这个错误代码\n");
		}
	}
}

/* 产生访存请求 */
void do_request()
{
	/* 随机产生请求地址 */
	//产生的虚地址是0~64*4-1  还有再进一步判断属于哪一页
	ptr_memAccReq->virAddr = random() % VIRTUAL_MEMORY_SIZE;
	/* 随机产生请求类型 */
	switch (random() % 3)
	{
		case 0: //读请求
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("产生请求：\n地址：%u\t类型：读取\n", ptr_memAccReq->virAddr);
			break;
		}
		case 1: //写请求
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* 随机产生待写入的值 */
			//这个值应该是一个字节,8位    u代表整型无符号数
			ptr_memAccReq->value = random() % 0xFFu;
			printf("产生请求：\n地址：%u\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->value);
			break;
		}
		case 2:
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%u\t类型：执行\n", ptr_memAccReq->virAddr);
			break;
		}
		default:
			break;
	}	
}

/*************************************************************************/
void new_do_request(){
	unsigned long address;
	int type;
	int value;
	scanf("%d %d %d",&address,&type,&value);
	ptr_memAccReq->virAddr = address % VIRTUAL_MEMORY_SIZE;
	switch (type)
	{
		case 0: //读请求
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("产生请求：\n地址：%u\t类型：读取\n", ptr_memAccReq->virAddr);
			break;
		}
		case 1: //写请求
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* 随机产生待写入的值 */
			//这个值应该是一个字节,8位    u代表整型无符号数
			ptr_memAccReq->value = value % 0xFFu;
			printf("产生请求：\n地址：%u\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->value);
			break;
		}
		case 2:
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%u\t类型：执行\n", ptr_memAccReq->virAddr);
			break;
		}
		default:
			break;
	}	

}
/*************************************************************************/


/* 打印页表 */
void do_print_info()
{
	unsigned int i, j, k;
	char str[4];
	printf("页号\t块号\t装入\t修改\t保护\t计数\t辅存\n");
	for (i = 0; i < PAGE_SUM; i++)
	{
		printf("%u\t%u\t%u\t%u\t%s\t%u\t%u\n", i, pageTable[i].blockNum, pageTable[i].filled, 
			pageTable[i].edited, get_proType_str(str, pageTable[i].proType), 
			pageTable[i].count, pageTable[i].auxAddr);
	}
}
/*************************************************************************/
//打印辅存
void do_print_auxiliaryStorage(){
	BYTE c[4];
	int i,readnum;
	if (fseek(ptr_auxMem, 0L, SEEK_SET) < 0){
		printf("读取虚存失败\n");
	}
	printf("in\n");
	for(i=0;i<64;i++){
		if((readnum = fread(c, sizeof(BYTE), PAGE_SIZE, ptr_auxMem))==PAGE_SIZE){
			printf("%02X %02X %02X %02X\n", c[0],c[1],c[2],c[3]);
		}
		else{
			printf("读取虚存失败\n");
		}
	}
}
//打印实存
void do_print_memory(){
	int i,j = 0;
	printf("实存内容如下:\n");
	for(i=0;i<128;i++){
		j++;
		if(j == 4){
			printf("%02X\n", actMem[i]);
			j = 0;
		}
		else{
			printf("%02X ", actMem[i]);
		}
	}
}
/*************************************************************************/

/* 获取页面保护类型字符串 */
char *get_proType_str(char *str, BYTE type)
{
	if (type & READABLE)
		str[0] = 'r';
	else
		str[0] = '-';
	if (type & WRITABLE)
		str[1] = 'w';
	else
		str[1] = '-';
	if (type & EXECUTABLE)
		str[2] = 'x';
	else
		str[2] = '-';
	str[3] = '\0';
	return str;
}

int main(int argc, char* argv[])
{
	char c;
	int i;
	//if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "r+")))
	//创建辅存文件
	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "w+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}
	//初始化页表，辅存和实存
	do_init();
	//打印页表信息
	do_print_info();
	//为访存请求申请空间
	ptr_memAccReq = (Ptr_MemoryAccessRequest) malloc(sizeof(MemoryAccessRequest));
	/* 在循环中模拟访存请求与处理过程 */
	while (TRUE)
	{
		//do_request();
/***************************************************************************************/
		new_do_request();
/***************************************************************************************/
		do_response();
		c = getchar();
		printf("按Y打印页表，按其他键不打印...\n");
		if ((c = getchar()) == 'y' || c == 'Y')
			do_print_info();
		while (c != '\n')
			c = getchar();
/***************************************************************************************/
		printf("按A打印辅存，按其他键不打印...\n");
		if ((c = getchar()) == 'a' || c == 'A')
			do_print_auxiliaryStorage();
		while (c != '\n')
			c = getchar();
		printf("按M打印实存，按其他键不打印...\n");
		if ((c = getchar()) == 'm' || c == 'M')
			do_print_memory();
		while (c != '\n')
			c = getchar();
/***************************************************************************************/
		printf("按X退出程序，按其他键继续...\n");
		if ((c = getchar()) == 'x' || c == 'X')
			break;
		while (c != '\n')
			c = getchar();
		//sleep(5000);
	}

	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}
	return (0);
}
