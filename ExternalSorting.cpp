#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <algorithm>
#include <bits/stdc++.h>
using namespace std;

#define MAX_WAYS 100 //最多归并路数
#define MAX_NUM  100 * 1024 * 1024	//内存最大使用量
#define BUF_SIZE  110000000   //每次从文本中取出的字符数
#define EACH_DOUBLE_NUM	BUF_SIZE/10	 //每次从文本中取出的浮点数 ，假设每个浮点数大小约10位


long long temp_file_num[1000];   //记录每次转化的浮点数
long long buf_size_temp[100] = {0};   //记录每次实际取出的字符数
long long sum_size = 0;		//记录读出的总的文件大小
long long sum = 0;		//记录读出浮点数的个数
int numRuns = 0;		//记录生成的临时文件个数，即总归并路数
bool readIsOver = false;	//用来标记问价是否读完
int illegal_input_num = 0;	//记录非法输入个数
int iFile = 0;	//生成临时文件的个数，文件名


static int ls[MAX_WAYS];	//败者树
double power[1000] ={0};	//atoF函数中的指数
int sortThread;		//处理、读入数据的线程总数
int itemProduced;   //基数排序线程数目
char readInFile[1000] = ".\\Sort.param";	//配置参数文件


typedef struct run_t {
	double *buf;	// 输入缓冲区
	long long length;	// 缓冲区当前有多少个数
	int times;	// 记录归并排序时每个临时文件需要读的次数
	int x_times;   //记录归并排序时每个临时文件已经读的次数
	long long idx;  // 缓冲区的指针
} run_t;

struct ItemRepository{
	char str[8][BUF_SIZE+10];	 //保存每次读出的字符
	int readPosition;	//读数据位置.
	int writePosition;  //写数据位置
	int itemCounter;   //记录处理文件个数
	mutex mtx;	//互斥量，保护缓冲区
	mutex itemCounterMtx;   //互斥量，避免多个写线程同时操作
	condition_variable repoNotFull;		//条件变量，指示缓冲区不为满
	condition_variable repoNotEmpty;	//条件变量，指示缓冲区不为空
}gItemRepository;


char inputFile[1000]  =  "";	//待处理数据 输入文件名
char outputFile[1000] = "";    //保存结果数据 输出文件名
ofstream fout;
ifstream fin;

void initItem(ItemRepository *ir);     //初始化数据
void adjust(run_t **runs, int n, int s);	//败者树调整
void createLoserTree(run_t **runs, int n);	//创建败者树
void mergeSort(int num_of_runs, const char* file_out);	//归并排序
void radixSort(double input[], long long length);	//基数排序
double atoF(char s[]);	//将字符串转化为浮点数，返回数值
void readItem(ItemRepository *ir,int item);	//分块读取文件
void readTask();	//分块读文件任务
void writeItem(ItemRepository *ir);	//分块处理文件，将字符串转化为浮点数，基数排序，写入文件
void writeTask();	//分块处理文件任务
int ftoA(double dnum, char* str, int len);	//将浮点数转化为字符串，返回字符串长度
int findThePoint(int left, int right, double dnum);	  //将浮点数转化为字符串时，二分查找返回小数点的位置


int main()
{
	//初始化数据
	cout<<"读取配置参数,初始化数据。。。"<<endl;
	initItem(&gItemRepository);
	cout<<"1个读线程"<<endl;
	cout<<sortThread - 1<<"个写线程"<<endl;

	clock_t s, e;
	s = clock();
	clock_t s1, e1;
	s1 = clock();

	fin.open(inputFile, ios::binary);
	if (!fin.is_open())
	{
		cout<<"can't open file "<<inputFile<<endl;
		exit(0);
	}

	//创建1个读线程，sortThread-1个写线程
	thread readT(readTask);
	thread writeT[100];
	for (int i = 0; i < sortThread - 1; i++)
	{
		writeT[i] = thread(writeTask);
	}
	readT.join();
	for (int i = 0; i < sortThread - 1; i++)
	{
		writeT[i].join();
	}

	fin.close();
	e1 = clock();
	cout<<"输入总用时："<<(double)(e1 - s1) / CLOCKS_PER_SEC<<endl;
	cout<<"归并段的个数为："<<numRuns<<endl;
	cout<<"文件总大小为："<<sum_size<<endl;
	cout<<"总的浮点数为："<<sum<<endl;
	cout<<"非法输入的个数为："<<illegal_input_num<<endl;

	//归并排序
	clock_t s2, e2;
	s2 = clock();
	mergeSort(numRuns, outputFile);
	e2 = clock();
	cout<<"归并总用时："<<(double)(e2 - s2) / CLOCKS_PER_SEC<<endl;
	e = clock();
	cout<<"外排用时："<<(double)(e - s) / CLOCKS_PER_SEC<<endl;

	//删除文件
	for (int i =0; i < numRuns; i++)
	{
		remove("i+100");
	}

	return 0;
}



//功能：读入配置参数，初始化数据
//参数：ItemRepository结构体
//返回值：空
void initItem(ItemRepository *ir)
{
	//初始化读写位置
	ir->writePosition = 0;
	ir->readPosition = 0;
	ir->itemCounter = 0;

	//初始化power数组 1e-308到1e+308
	power[0] = 1e-308;
	for (int i = 1; i < 617; i++)
	{
		power[i] = power[i-1] * 10.0;
	}

	char line[10][1000];
	char strtemp[1000];
	ifstream readin;
	readin.open(readInFile);

	//读入第一行，确定输入文件名
	readin.getline(line[0], sizeof(line[0]));
	sscanf(line[0], "%*100[^=]%100[^;]", strtemp);
	int k = 0;
	for (int j = 1; j < strlen(strtemp); j++)
	{
		if (strtemp[j] =='\\')
		{
			inputFile[k++] = '\\';
			inputFile[k++] = '\\';
		}
		else
		{
			inputFile[k++] = strtemp[j];
		}
	}
	inputFile[k] = '\0';

	//读入第二行，确定输出文件名
	readin.getline(line[1], sizeof(line[1]));
	sscanf(line[1], "%*100[^=]%100[^;]", strtemp);
	k = 0;
	for (int j = 1; j < strlen(strtemp); j++)
	{
		if (strtemp[j] =='\\')
		{
			outputFile[k++] = '\\';
			outputFile[k++] = '\\';
		}
		else
		{
			outputFile[k++] = strtemp[j];
		}
	}
	outputFile[k] = '\0';

	//读入第三行，确定写线程数目
	readin.getline(line[2], sizeof(line[2]));
	sscanf(line[2], "%*100[^=]=%d[^;]", &sortThread);
	itemProduced = sortThread;
	readin.close();
}

//功能：单线程读取文件
//参数：ItemRepository结构体，产生文件标号
//返回值：空
void readItem(ItemRepository *ir,int item)
{
	//读文件线程（生产者）等待“缓冲区不为满”这一条件发生
	unique_lock<mutex> lock(ir->mtx);
	while (((ir->writePosition + 1) % itemProduced)== ir->readPosition)
	{
		(ir->repoNotFull).wait(lock);
	}

	//分块读取文件
	fin.read((ir->str)[item], BUF_SIZE * sizeof(char));
	ir->writePosition ++;
	//buf_size_temp[item] = fin.gcount();
	buf_size_temp[item] = BUF_SIZE;

	//找到最后一个完整的浮点数的位置，将文件指针倒退到此位置
	int offset = 0;
	for (long long tp = buf_size_temp[item] - 1; tp >= 0; tp --)
	{
		if ((ir->str)[item][tp] == '\n')
		{
			buf_size_temp[item]  = tp + 1;
			(ir->str)[item][buf_size_temp[item]] = '\0';
			break;
		}
		offset--;
	}
	streampos sp = fin.tellg();
	fin.seekg(offset, ios::cur);

	sum_size = sum_size + buf_size_temp[item];

	//写入位置若是在队列最后则重新设置为初始位置
	if (ir->writePosition == itemProduced)
		ir->writePosition = 0;

	//通知读线程（消费者）产品库不为空
	(ir->repoNotEmpty).notify_all();
	lock.unlock();
}

//功能：调用readItem函数分块读取文件
//参数：空
//返回值：空
void readTask()
{
	//分块读取，循环生产itemProduced个文件
	int item = 0;
	while (fin.peek() != EOF)
	{
		readItem(&gItemRepository, item);
		item ++;
		if (item == itemProduced)
		{
			item = 0;
		}
		numRuns ++;
	}
	readIsOver = true;
}

//功能：处理分块文件，并排序写入
//参数：ItemRepository结构体
//返回值：空
void writeItem(ItemRepository *ir)
{
	run_t *runs = (run_t *)malloc(sizeof(run_t) * 100);
	runs->buf = (double *)calloc(EACH_DOUBLE_NUM, sizeof(double));
	int item = 0;
	std::unique_lock<std::mutex> lock(ir->mtx);

	//写文件线程（消费者）等待“缓冲区不为空”这一条件发生
	while(ir->writePosition == ir->readPosition)
	{
		(ir->repoNotEmpty).wait(lock);
	}
	item = ir->readPosition;
	(ir->readPosition)++;


	long long jp = 0, ip = 0, kp = 0;
	long long pre = 0;	//记录处理字符串的位置
	bool flag = false;	//标记是否为第一次遇见非法字符
	char illegal_input[100]; //保存非法输入字符


	//将字符串转化为浮点数
	for (ip = 0; ip < buf_size_temp[item]; ip++)
	{
		int tmp = 0;

		//判断非法输入
		if ((((ir->str)[item][ip] < '0') || ((ir->str)[item][ip] > '9'))
			&& ((ir->str)[item][ip] != '-')&& ((ir->str)[item][ip] != '+')
			&& ((ir->str)[item][ip] != '\r') && ((ir->str)[item][ip] != 'e')
			&& ((ir->str)[item][ip] != 'E') && ((ir->str)[item][ip] != '.')
			&& ((ir->str)[item][ip] != '\n')){
			if (!flag)
			{
				//若发现一个非法字符，则找到该非法字符串并输出，并移动ip位置
				flag = true;
				for (long long mp = pre; mp < buf_size_temp[item]; mp++)
				{
					illegal_input[mp - pre] = (ir->str)[item][mp];
					if ((ir->str)[item][mp] == '\n')
					{
						illegal_input[mp - pre] = '\0';
						illegal_input_num ++;
						cout<<"非法输入："<<illegal_input<<endl;
						ip =  mp;
						break;
					}
				}
			}
		}

		//若遇到‘\n’,则表示读完一行完整的字符串，转化为浮点数
		if ((ir->str)[item][ip] == '\n')
		{
			if (flag)
				pre = ip + 1;
			else
			{
				(ir->str)[item][ip] ='\0';
				runs->buf[jp] = atoF((ir->str)[item]+pre);
				pre = ip+1;
				jp++;
			}
			flag = false;

		}
		else if ((ir->str)[item][ip] == '\r')
		{
			(ir->str)[item][ip] = '\0';
		}
	}
	//记录每个小文件中浮点数的个数
	runs->length = jp;
	temp_file_num[iFile] = jp;

	//对每个小文件进行基数排序
	radixSort(runs->buf, runs->length);

	//将有序数组写入每个小文件中
	ofstream out_temp;
	char out_temp_file[10];  //小文件临时文件名
	sprintf(out_temp_file, "%d", iFile+100);
	out_temp.open(out_temp_file, ios::binary);
	if (!out_temp.is_open())
	{
		printf("FILE OPEN ERROR!\n");
		exit(0);

	}
	out_temp.write((char*)runs->buf, runs->length * sizeof(double));
	out_temp.close();

	sum = sum + temp_file_num[iFile]; //记录读入浮点数总的个数
	free(runs->buf);
	free(runs);
	iFile ++; //文件数加1

	//读出位置若是在队列最后则重新设置为初始位置
	if (ir->readPosition >= itemProduced)
		ir->readPosition = 0;
	(ir->repoNotFull).notify_all();
	lock.unlock();
}

//功能：调用writeItem函数将分块读取的数据处理有序后，写入小文件中
//参数：空
//返回值：空
void writeTask()
{
	bool readyToExit = false;
	while(1)
	{
		unique_lock<mutex> lock(gItemRepository.itemCounterMtx);
		//判断处理的文件数是否达到划分的文件数，即所有文件是否都处理完毕
		if ((!readIsOver) || (gItemRepository.itemCounter < numRuns))
		{
			 writeItem(&gItemRepository);
			 (gItemRepository.itemCounter) ++;
		}
		else
		{
			readyToExit = true;
		}
		lock.unlock();
		if(readyToExit)
		{
			break;
		}
	}

}


//功能：将字符串转化为浮点数
//参数：待转化的字符串
//返回值：转化后的浮点数
double atoF(char s[])
{
	int i, cnt = 0;
	int sign_1;		//标记浮点数的正负，-1表示负，1表示正
	bool find_frist_non_zero = false;	//找到第一个有效数字，即非0数字
	bool sign_2 = true;  //标记e后的数是否为正数
	double rate = 1.0;
	bool bDot = false;	//标记是否出现小数点
	bool bE = false;	//表示是否出现科学记数法表示的浮点数，即是否有e
	double ret = 0.0;	//保存浮点数
	int exp = 0;

	//跳过数字前的空格
	for(i = 0; isspace(s[i]); i++);

	//获得此浮点数的正负
	sign_1 = s[i] == '-' ? -1 : 1;
	if(s[i] == '-' || s[i] == '+')
		i++;
	for(ret = 0.0; s[i] != '\0'; i++)
	{
		if(s[i] == '.')
		{
			bDot = true;
			i++;
		}
		//若此浮点数使用科学记数法表示，则标记bE，sign_2
		if((s[i] == 'e') || (s[i] == 'E'))
		{
			bE = true;
			i++;
			if((s[i] == '-') || (s[i] == '+'))
			{
				sign_2 = s[i]== '-' ? false : true;
				i++;
			}
		}
		if(!bE)
		{
			//找到第一个非0数字
			if (((s[i]-'0') != 0) && (!find_frist_non_zero))
			{
				find_frist_non_zero = true;
			}
			if (find_frist_non_zero && (cnt < 12)) //只处理到第11个字符
			{
				cnt ++;
			}
			//处理精度问题，在若第11位大于等于5，则前一位直接加1，当前为置零
			if(!bDot)
			{
				if (cnt == 11)
				{
					if ((s[i]-'0') >= 5)
					{
						ret = (ret + (double)1.0) * 10.0;
						continue;
					}

				}
				ret = (ret * 10.0) + (s[i] - '0'); //若只是乘以10，会有精度损失
			}
			else
			{
				if (cnt == 11)
				{
					if ((s[i]-'0') >= 5)
					{
						ret = ret + ((double)1.0) / rate;
						rate = rate * 10.0;
						continue;
					}

				}
				rate = rate * 10.0;
				ret = ret + ((double) (s[i] - '0')) / rate; //若不强制转化成double，会损失精度
			}
		}
		//若出现科学记数法表示的浮点数，则用exp保留指数
		else
		{
			exp = 10 * exp + (s[i] - '0');
		}
	}
	if(sign_2)
		ret = ret * power[308 + exp];
	else
		ret = ret * power[308 - exp];

	return sign_1 * ret;
}

//功能：对分块读出的浮点数进行基数排序
//参数：带排序数组，数组长度
//返回值：空
void radixSort(double input[], long long length)
{
	double *tmp_12 = (double *)malloc((length+10) * sizeof(double));
	long long negCnt = 0; //记录负数的个数
	int bucket[65536];

	//将64位划分成4个字节，1次排序1个字节
	for (int i = 0; i < 4; i++)
	{
		//计数器清零
		for (int j = 0; j < 65536; j++)
			bucket[j] = 0;
		//统计计数
		int digit = i * 16;
		for (long long j = 0; j < length; j++)
			bucket[reinterpret_cast<unsigned long long&>(input[j])>>digit & 0xffff]++;
		//计算各段的起始位置
		for (int j = 1; j < 65536; j++)
			bucket[j] = bucket[j-1] + bucket[j];
		//复制到临时数组，并记录负数的个数
		for (long long j= length - 1; j >= 0; j--)
		{
			int temp = --bucket[reinterpret_cast<unsigned long long&>(input[j])>>digit & 0xffff];
			tmp_12[temp] = input[j];
			if ((i == 0) && input[j] < 0)
			{
				negCnt++;
			}
		}
		//将临时数组中的数复制到原数组
		//在最后一遍排序时得到的顺序：正数从小到大，负数从大到小
		//所以我们将正负数的位置翻转到从小到大排列的正确位置
		for (long long j = 0; j < length; j++)
		{
			if (i == 3)
			{
				if (tmp_12[j] < 0.0)
					input[length - 1 - j] = tmp_12[j];
				else
					input[negCnt + j] = tmp_12[j];
			}
			else
				input[j] = tmp_12[j];
		}
	}
	free(tmp_12);
}

//功能：将各段顺串进行归并排序，形成最终的有序文件
//参数：归并路数（分割的小文件数），最终输出文件名
//返回值：空
void mergeSort(int num_of_runs, const char* file_out)
{
	//计算每次取出浮点数的个数，如果大于最后一个文件的浮点数数目，则以最后一个文件为标准取出
	long long length_per_run = (MAX_NUM/20) / num_of_runs;
	if (length_per_run > temp_file_num[num_of_runs - 1] )
	{
		length_per_run = temp_file_num[num_of_runs - 1];
	}
	cout<<"每次取出字符数："<<length_per_run<<endl;

	//初始化结构体run_t，并计算每个文件需要读取的次数
	run_t **runs = (run_t **)malloc(sizeof(run_t *)*(num_of_runs+1));
	for (int i = 0; i < num_of_runs; i++)
	{
		runs[i] = (run_t *)malloc(sizeof(run_t));
		runs[i]->buf = (double *)calloc((length_per_run +100),  sizeof(double));
		runs[i]->x_times = 0;
		runs[i]->times =(int) (temp_file_num[i] / length_per_run);
		int temp = (int)(temp_file_num[i] % length_per_run);
		if (temp != 0)
		{
			runs[i]->times ++;
		}
	}

	// 将每个顺串文件的length_per_run数据读到缓冲区中
	ifstream in[100];
	char file_name[20];
	for (int i = 0; i < num_of_runs; i++)
	{
		sprintf(file_name, "%d", i+100);
		in[i].open(file_name, ios::binary);
		in[i].read((char *)runs[i]->buf, length_per_run * sizeof(double));
		runs[i]->x_times ++;
		runs[i]->length = length_per_run;
		runs[i]->idx = 0;
	}

	//创建败者树进行排序
	createLoserTree(runs, num_of_runs);

	cout<<"正在写入最终文件中。。。"<<endl;

	ofstream final;
	final.open(file_out, ios::binary);
	int live_runs = num_of_runs;
	int pbuf_len = 0;
	char *pbuf = (char *)malloc((MAX_NUM + 1000) * sizeof(char));

	while (live_runs > 0)
	{
		//分批写入最终文件中，当缓冲区中的字符数大于MAX_NUM时写入
		pbuf_len += ftoA(runs[ls[0]]->buf[runs[ls[0]]->idx++], &(*(pbuf+ pbuf_len)), 9);
		if (pbuf_len >= MAX_NUM)
		{
			final.write(pbuf, pbuf_len);
			pbuf_len = 0;
		}
		//如果某一顺串文件中数据都处理结束，则将ls[0]置为最大值
		if ((runs[ls[0]]->x_times  == runs[ls[0]]->times) && (runs[ls[0]]->idx == runs[ls[0]]->length))
		{
			runs[ls[0]]->buf[runs[ls[0]]->idx] = DBL_MAX;
			live_runs--;
		}
		//如果当前顺串中读取的数据处理完，则继续读取
		else if (runs[ls[0]]->idx == runs[ls[0]]->length)
		{
			long long ac_length_per_run = length_per_run;
			//若为最后一次读取当前文件，则取出剩余的浮点数
			if (runs[ls[0]]->x_times  == (runs[ls[0]]->times - 1))
			{
				ac_length_per_run = temp_file_num[ls[0]] - length_per_run * runs[ls[0]]->x_times;
			}
			in[ls[0]].read((char *)runs[ls[0]]->buf, ac_length_per_run * sizeof(double));
			runs[ls[0]]->x_times ++;
			runs[ls[0]]->length = ac_length_per_run;
			runs[ls[0]]->idx = 0;
		}
		//调整败者树
		adjust(runs, num_of_runs, ls[0]);
	}
	final.write(pbuf, pbuf_len);
	free(pbuf);

	//关闭文件，释放内存空间
	for (int i = 0; i < num_of_runs; i++)
	{
		in[i].close();
		free(runs[i]->buf);
		free(runs[i]);
	}
	free(runs);
	final.close();
}

//功能：创建败者树
//参数：run_t结构体， 有序文件的个数
//返回值：空
void createLoserTree(run_t **runs, int n)
{
	for (int i = 0; i < n; i++)
	{
		ls[i] = -1;
	}
	for (int i = n-1; i >= 0; i--)
	{
		adjust(runs, n, i);
	}
}

//功能：构建败者树，每次读入新值调整败者树
//参数:run_t结构体，有序文件个数，当前文件下标
//返回值：空
void adjust(run_t **runs, int n, int s)
{
	int t = (s + n) / 2;
	int tmp;

	while (t != 0)
	{
		if (s == -1)
			break;
		if (ls[t] == -1 || runs[s]->buf[runs[s]->idx] >
			runs[ls[t]]->buf[runs[ls[t]]->idx])
		{
			tmp = s;
			s = ls[t];
			ls[t] = tmp;
		}
		t /= 2;
	}
	ls[0] = s;
}

//功能：将浮点数转化成字符串，格式：科学记数法表示，并保留9位小数
//参数：待转化的小数，存储小数的字符数组,科学记数法保留小数的位数
//返回值：转化后的字符串的长度
int ftoA(double dnum, char* str, int len)
{
	int arrayNum[100]; //记录每次取出浮点数的整数部分，最多100位
	int pointPos=0;//小数点的位置
	bool sign_1 = false; //记录正数or负数
	bool sign_2 = false; //记录大于1or小于1 即科学记数法e后面的数为正or负
	double lastDigit = 5.0;
	len = len + 2;
	//如果为负数，则转化为整数
	if(dnum < 0)
	{
		dnum = -dnum;
		sign_1 = true;
	}
	if (dnum == 0.0 || (dnum >= 1.0 && dnum < 10.0))
	{
		sign_2 = true;
	}
	//二分查找小数点的位置
	if (dnum >= 10.0)
	{
		int temp = findThePoint(0, 308, dnum);
		dnum = dnum * power[temp];
		pointPos = 308 - temp;
		sign_2 = true;
	}
	if ((dnum < 1.0) && (dnum != 0.0))
	{
		int temp = findThePoint(308, 616, dnum);
		dnum = dnum * power[temp];
		pointPos = temp - 308;
	}
	//len+1 位加上5
	lastDigit = lastDigit * power[308 - len - 1];

	//每次取出当前浮点数的整数部分并记录在数组中
	for(int i = 0; i < len; i++)
	{
		if (i == 1)
		{
			arrayNum[i] = -2;   //当前位为小数点
			continue;
		}
		int num = (int)dnum;
		arrayNum[i] = num;
		dnum = dnum - (int)dnum;
		dnum = (dnum + lastDigit) * 10.0;
	}

	//将整数数组转化为字符串
	int k = 0;
	if (sign_1) //判断是否为负数
	{
		str[k] = '-';
		k++;
	}
	for(int i = 0;i < len; i++)
	{
		if(i == 1)
		{
			str[k++] = '.';
			continue;
		}
		str[k++] = arrayNum[i]+'0';
	}
	str[k++] = 'E';

	//判断指数部分的正负
	if (sign_2)
		str[k++] = '+';
	else
		str[k++] = '-';

	//将指数部分转化为字符串 范围0-308
	if (pointPos < 10)
	{
		str[k++] = '0';
		str[k++] = '0';
		str[k++] =  pointPos + '0';
	}
	else if(pointPos < 100)
	{
		str[k++] = '0';
		str[k++] = pointPos / 10 + '0';
		str[k++] = pointPos % 10 + '0';
	}
	else if(pointPos < 1000)
	{
		str[k++] = pointPos / 100 % 10 + '0';
		str[k++] = pointPos / 10 % 10 + '0';
		str[k++] = pointPos % 10 + '0';
	}
	str[k++]='\n';
	str[k]='\0';
	return strlen(str);
}

//功能：二分查找小数点的位置
//参数：查找的起始位置，终止位置，带查找的数
//返回值：返回当前数指数的大小（小数点的位置）
int findThePoint(int left, int right, double dnum)
{
	int mid = (left + right) / 2;;
	double tmp = 0.0;
	while (left <= right)
	{
		mid = (left + right) / 2;
		tmp = dnum * power[mid];
		if (tmp >= 1.0 && tmp < 10.0)
			break;
		if (tmp >= 10.0)
		{
			right = mid - 1;
		}
		else if (tmp < 1.0)
		{
			left = mid + 1;
		}
	}
	return mid;
}

