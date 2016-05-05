#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include "vmm.h"
#include "time.h"

/* 页表 */
FirstPageTableItem firstPageTable[8];
PageTableItem pageTable[PAGE_SUM];
/* 实存空间 */
BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
FILE *ptr_auxMem;
/* 物理块使用标识 */
BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
Ptr_MemoryAccessRequest ptr_memAccReq;

int pid;


/* 初始化环境 */
void do_init()
{
	int i, j;
	srandom(time(NULL));
	/********************************************************************************/
	//初始化辅存,不然程序运行不了
	unsigned char c;
	for (i = 0; i < 64 * 4; i++) {
		c = random() % 0xFFu;
		fprintf(ptr_auxMem, "%c", c);
	}
	/********************************************************************************/



	//初始化二级页表
	for (i = 0; i < PAGE_SUM; i++)
	{
		pageTable[i].pageNum = i;
		pageTable[i].filled = FALSE;
		pageTable[i].edited = FALSE;
		pageTable[i].count = 0;
		pageTable[i].LRU_flag = 0;
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
		//pageTable[i].auxAddr = i * PAGE_SIZE * 2;
		pageTable[i].auxAddr = i * PAGE_SIZE;
		/***********************************************************************************/
		/***********************************************************************************/
		//随机产生进程号0~9
		pageTable[i].proccessNum = random() % 10;
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

	//初始化一级页表
	for(i=0;i<8;i++){
		firstPageTable[i].firstPageNum = i;
		firstPageTable[i].secondPageNum[0] = 8*i+0;
		firstPageTable[i].secondPageNum[1] = 8*i+1;
		firstPageTable[i].secondPageNum[2] = 8*i+2;
		firstPageTable[i].secondPageNum[3] = 8*i+3;
		firstPageTable[i].secondPageNum[4] = 8*i+4;
		firstPageTable[i].secondPageNum[5] = 8*i+5;
		firstPageTable[i].secondPageNum[6] = 8*i+6;
		firstPageTable[i].secondPageNum[7] = 8*i+7;
	}

}


/* 响应请求 */
void do_response()
{
	Ptr_PageTableItem ptr_pageTabIt;
	unsigned int pageNum, offAddr;
/************************************************************************************/
	unsigned int firstPageNum, firstOffAddr;
/************************************************************************************/
	unsigned int actAddr;

	/* 检查地址是否越界 */
	if (ptr_memAccReq->virAddr < 0 || ptr_memAccReq->virAddr >= VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_OVER_BOUNDARY);
		return;
	}

	/************************************************************************************/
	printf("请求进程号: %u\n", ptr_memAccReq->proccessNum);
	/************************************************************************************/

	/* 计算页号和页内偏移值 */
	// pageNum = ptr_memAccReq->virAddr / PAGE_SIZE;
	// offAddr = ptr_memAccReq->virAddr % PAGE_SIZE;
	// printf("页号为：%u\t页内偏移为：%u\n", pageNum, offAddr);
/************************************************************************************/
	firstPageNum = ptr_memAccReq->virAddr / 32;
	firstOffAddr = (ptr_memAccReq->virAddr % 32) / PAGE_SIZE;
	offAddr = ptr_memAccReq->virAddr % PAGE_SIZE;
	printf("一级页表页号为：%u\t一级页表页内偏移为：%u\n", firstPageNum, firstOffAddr);
	printf("二级页表页号为：%u\t二级页表页内偏移为：%u\n", firstPageTable[firstPageNum].secondPageNum[firstOffAddr], offAddr);
/************************************************************************************/

	/* 获取对应页表项 */
	//ptr_pageTabIt = &pageTable[pageNum];
	ptr_pageTabIt =  &pageTable[firstPageTable[firstPageNum].secondPageNum[firstOffAddr]];


	/***********************************************************************************/
	if (ptr_memAccReq->proccessNum != ptr_pageTabIt->proccessNum) {
		ptr_pageTabIt->count++;
		ptr_pageTabIt->LRU_flag = 1;
		printf("权限不够,无法操作其他进程数据\n");
		return;
	}
	/***********************************************************************************/

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
		ptr_pageTabIt->LRU_flag = 1;
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
		ptr_pageTabIt->LRU_flag = 1;
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
		ptr_pageTabIt->LRU_flag = 1;
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
			ptr_pageTabIt->LRU_flag = 1;

			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	//do_LFU(ptr_pageTabIt);
	do_LRU(ptr_pageTabIt);
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



void do_LRU (Ptr_PageTableItem ptr_pageTabIt){
	unsigned int i,page;
	printf("没有空闲物理块，开始进行LRU页面替换...\n");
	for (i = 0,page = 0; i < PAGE_SUM ; i++){
		if (pageTable[i].filled && pageTable[i].LRU_flag ==0){
			page = i;
			break;//如果有多个未被使用，选择其中最靠前的一页
		}
	}

	if (i==PAGE_SUM){ //没有找到未被使用的,优先选择最靠前的	
		for (i = 0; i < PAGE_SUM; i++){
			if (pageTable[i].filled){
				page = i;
				break;
			}
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
	pageTable[page].LRU_flag = 0;


	/* 读辅存内容，写入到实存 */
	do_page_in(ptr_pageTabIt, pageTable[page].blockNum);
	
	/* 更新页表内容 */
	ptr_pageTabIt->blockNum = pageTable[page].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->LRU_flag = 0;
	printf("页面替换成功\n");

}


void do_LRU_aging(){
	//
	

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
void new_do_request() {
	unsigned long address;
	int type;
	int value;
	unsigned int proccessnum;
	/*************************************************************************/
	//进程
	printf("输入请求: address type proccessnum value\n");
	scanf("%d%d%d%d", &address, &type, &proccessnum, &value);
	kill(pid, SIGUSR1);

	//上面四个打到文件

	char temp_str[10000] = { 0 };
	sprintf(temp_str, "%d\n%d\n%d\n%d", address, type, proccessnum, value);
	int temp_fifo;
	if ((temp_fifo = open("/tmp/temp_var4", O_WRONLY)) < 0)
		printf("opening failed");
	if (write(temp_fifo, temp_str, 10000) < 0)
		printf("/tmp/temp_info write failed");
	close(temp_fifo);
}
/*************************************************************************/


/* 打印页表 */
void do_print_info()
{
	unsigned int i, j, k;
	char str[4];
	printf("页号\t块号\t装入\t修改\t保护\t计数\t标志位\t辅存\t进程号\n");
	for (i = 0; i < PAGE_SUM; i++)
	{
		printf("%u\t%u\t%u\t%u\t%s\t%u\t%u\t%u\t%u\n", i, pageTable[i].blockNum, pageTable[i].filled,
			pageTable[i].edited, get_proType_str(str, pageTable[i].proType),
			pageTable[i].count, pageTable[i].LRU_flag,pageTable[i].auxAddr, pageTable[i].proccessNum);
	}
}
/*************************************************************************/
//打印辅存
void do_print_auxiliaryStorage() {
	BYTE c[4];
	int i, readnum;
	if (fseek(ptr_auxMem, 0L, SEEK_SET) < 0) {
		printf("读取虚存失败\n");
	}
	for (i = 0; i < 64; i++) {
		if ((readnum = fread(c, sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) == PAGE_SIZE) {
			printf("%02X %02X %02X %02X\n", c[0], c[1], c[2], c[3]);
		}
		else {
			printf("读取虚存失败\n");
		}
	}
}
//打印实存
void do_print_memory() {
	int i, j = 0;
	printf("实存内容如下:\n");
	for (i = 0; i < 128; i++) {
		j++;
		if (j == 4) {
			printf("%02X\n", actMem[i]);
			j = 0;
		}
		else {
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
/*********************************多进程函数****************************************/
int mark1 = 0; //用于进程间通信
int mark2 = 0;
void setMark1() {
	mark1 = 1;
}
void setMark2() {
	mark2 = 2;
}

void do_print_info_to_file()
{
	char temp_str[10000] = { 0 };
	char temp_str_out[10000] = { 0 };

	unsigned int i, j, k;
	char str[4];
	sprintf(temp_str, "页号\t块号\t装入\t修改\t保护\t计数\t标志位\t辅存\t进程号\n");
	strcat(temp_str_out, temp_str);
	for (i = 0; i < PAGE_SUM; i++)
	{
		sprintf(temp_str, "%u\t%u\t%u\t%u\t%s\t%u\t%u\t%u\t%u\n", i, pageTable[i].blockNum, pageTable[i].filled,
			pageTable[i].edited, get_proType_str(str, pageTable[i].proType),
			pageTable[i].count,pageTable[i].LRU_flag, pageTable[i].auxAddr, pageTable[i].proccessNum);
		strcat(temp_str_out, temp_str);
	}

	int temp_fifo;

	if ((temp_fifo = open("/tmp/temp_info", O_WRONLY)) < 0)

		printf("opening failed");



	if (write(temp_fifo, temp_str_out, 10000) < 0)

		printf("/tmp/temp_info write failed");



	close(temp_fifo);
}

void do_print_memory_to_file()
{
	char temp_str[10000] = { 0 };
	char temp_str_out[10000] = { 0 };

	int i, j = 0;
	sprintf(temp_str, "实存内容如下:\n");
	strcat(temp_str_out, temp_str);

	for (i = 0; i < 128; i++) {
		j++;
		if (j == 4) {
			sprintf(temp_str, "%02X\n", actMem[i]);
			strcat(temp_str_out, temp_str);
			j = 0;
		}
		else {
			sprintf(temp_str, "%02X ", actMem[i]);
			strcat(temp_str_out, temp_str);
		}
	}

	int temp_fifo;

	if ((temp_fifo = open("/tmp/temp_mem", O_WRONLY)) < 0)

		printf("opening failed");



	if (write(temp_fifo, temp_str_out, 10000) < 0)

		printf("/tmp/temp_mem write failed");



	close(temp_fifo);
}

void do_print_info_from_file()
{
	char temp_str[10000] = { 0 };

	int temp_fifo;
	if ((temp_fifo = open("/tmp/temp_info", O_RDONLY)) < 0)
		printf("open /tmp/temp_info failed");



	int count = 0;
	//读200个，关
	if ((count = read(temp_fifo, temp_str, 10000)) < 0)
		printf("read /tmp/temp_info failed");
	close(temp_fifo);
	printf("%s", temp_str);
}

void do_print_memory_from_file()
{
	char temp_str[10000] = { 0 };

	int temp_fifo;
	if ((temp_fifo = open("/tmp/temp_mem", O_RDONLY)) < 0)
		printf("open /tmp/temp_mem failed");
	int count = 0;
	//读200个，关
	if ((count = read(temp_fifo, temp_str, 10000)) < 0)
		printf("read /tmp/temp_mem failed");
	close(temp_fifo);
	printf("%s", temp_str);
}

//从文件中读取请求并处理，处理完之后把页表，辅存，实存数据写到三个文件里count 
void new_do_response() {
	if (mark1 == 0) {
		return;
	}
	else {
		mark1 = 0;
		//处理操作
		////////////////////////////////////////////////////////////////////////////////////////////////////
		unsigned long address;
		int type;
		int value;
		unsigned int proccessnum;

		//////////////////////////////////////////////////////////
		//读
		char temp_str[10000] = { 0 };

		int temp_fifo;
		if ((temp_fifo = open("/tmp/temp_var4", O_RDONLY)) < 0)
			printf("open /tmp/temp_var4 failed");



		int count = 0;
		//读200个，关
		if ((count = read(temp_fifo, temp_str, 10000)) < 0)
			printf("read /tmp/temp_var4 failed");
		close(temp_fifo);


		sscanf(temp_str, "%d%d%d%d", &address, &type, &proccessnum, &value);
		//////////////////////////////////////////////////////////

		ptr_memAccReq->proccessNum = proccessnum;
		/*************************************************************************/
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
		do_response();
		kill(getppid(), SIGUSR2);
		/////////////////////////////////////////////////////////////////////////////
		do_print_info_to_file();
		do_print_memory_to_file();
	}
}
int do_fork() {
	int fpid,i;
	time_t Last_Update_time,Now_time;


	long counter = 0;
	fpid = fork();
	time(&Last_Update_time);
	while (1) {
		if (fpid < 0) {
			printf("子进程创建失败\n");
			break;
		}
		else if (fpid == 0) {
			time(&Now_time);
			counter = Now_time-Last_Update_time;
			if (counter>=LRU_period){//超过刷新时间
				for ( i = 0; i < PAGE_SUM; ++i)
				{
					
						pageTable[i].LRU_flag = 0 ;
					
				}
				time(&Last_Update_time);
			} //定时刷新lru算法标志位

			new_do_response();
		}
		else {
			break;
		}
	}
	return fpid;
}



/***********************************************************************************/



int main(int argc, char* argv[])
{
	remove("/tmp/temp_mem");

	if (mkfifo("/tmp/temp_mem", 0666) < 0)

		printf("mkfifo failed");
	remove("/tmp/temp_info");

	if (mkfifo("/tmp/temp_info", 0666) < 0)

		printf("mkfifo failed");
	remove("/tmp/temp_var4");

	if (mkfifo("/tmp/temp_var4", 0666) < 0)

		printf("mkfifo failed");


	char c;
	int i;
	/**********************************************************************************/
	//用于进程间通信
	signal(SIGUSR1, setMark1);
	signal(SIGUSR2, setMark2);
	/**********************************************************************************/
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
	ptr_memAccReq = (Ptr_MemoryAccessRequest)malloc(sizeof(MemoryAccessRequest));
	/* 在循环中模拟访存请求与处理过程 */
	/**********************************************************************************/
	pid = do_fork();
	/**********************************************************************************/


	while (TRUE)
	{
		//do_request();
		/***************************************************************************************/
		new_do_request();
		/***************************************************************************************/
		//do_response();
		while (mark2 == 0);
		mark2 = 0;

		c = getchar();
		while (c != '\n')
			c = getchar();


		int flag_y = 0;
		int flag_m = 0;

		printf("按Y打印页表，按其他键不打印...\n");
		if ((c = getchar()) == 'y' || c == 'Y')
		{
			//do_print_info();
			do_print_info_from_file();
			flag_y = 1;
		}
		if (flag_y == 0)
		{
			char temp_str[10000] = { 0 };

			int temp_fifo;
			if ((temp_fifo = open("/tmp/temp_info", O_RDONLY)) < 0)

				printf("open /tmp/temp_info failed");



			int count = 0;

			//读200个，关

			if ((count = read(temp_fifo, temp_str, 10000)) < 0)

				printf("read /tmp/temp_info failed");

			close(temp_fifo);
		}

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
		{
			//do_print_memory();
			do_print_memory_from_file();
			flag_m = 1;
		}

		if (flag_m == 0)
		{
			char temp_str[10000] = { 0 };

			int temp_fifo;
			if ((temp_fifo = open("/tmp/temp_mem", O_RDONLY)) < 0)

				printf("open /tmp/temp_mem failed");



			int count = 0;

			//读200个，关

			if ((count = read(temp_fifo, temp_str, 10000)) < 0)

				printf("read /tmp/temp_info failed");

			close(temp_fifo);
		}


		while (c != '\n')
			c = getchar();
		/***************************************************************************************/
		printf("按X退出程序，按其他键继续...\n");
		if ((c = getchar()) == 'x' || c == 'X')
			break;
		while (c != '\n')
			c = getchar();
	}

	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}
	return (0);
}
