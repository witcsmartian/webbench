/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0;
int speed=0;
int failed=0;
int bytes=0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;
int clients=1;
int force=0;
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
   timerexpired=1;
}	

static void usage(void)//向屏幕打印出提示信息
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};

int main(int argc, char *argv[])//主函数
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)//argc纪录了用户在运行程序命令中输入的参数的个数 输入参数argc==1
 {
	  usage();//向屏幕打印出提示信息
          return 2;//返回为2 参数错误·
 } 

 while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   case  0 : break;
   case 'f': force=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);
   case 't': benchtime=atoi(optarg);break;	     
   case 'p': 
	     /* proxy server parsing server:port */   //代理服务器解析服务器：端口	
             tmp=strrchr(optarg,':');// 在optarg所指的字符串中 找到最后出现':'的位置，若找不到 则返回为NULL	
             proxyhost=optarg;
	     if(tmp==NULL)
	     {
		     break;
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;//int atio(const char *str)  将字符串抓换成整数形式 
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;
  }
 }
 
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 build_request(argv[optind]);
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();
}

void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);
  bzero(request,REQUEST_SIZE);

  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;//复制操作 
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }
		  
  strcat(request," ");//连接	
  if(NULL==strstr(url,"://"))
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);//地址中没有"://"	

	  exit(2);
  }
  if(strlen(url)>1500)//地址太长
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  if(proxyhost==NULL)
	  /*
	  int strncasecmp(const char *s1,const char *s2,size_t n)
	  
	  用来比较参数s1 s2字符串前n个字符，比较时自动忽略大小写	
	  1、相等 返回0；
	  2、s1>s2 返回>0
	  3、s1<s2 返回<0	
         */
	   if (0!=strncasecmp("http://",url,7)) //地址不是以 http://开头	   
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;
  /* printf("%d\n",i); */

  if(strchr(url+i,'/')==NULL) {//地址不是以'/'结尾
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                              }
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
	   /*
	   定义函数：char * index(const char *s, int c);
           函数说明：index()用来找出参数s 字符串中第一个出现的参数c 地址，
	   然后将该字符出现的地址返回。字符串结束字符(NULL)也视为字符串一部分。
	   */
   {
	   strncpy(host,url+i,strchr(url+i,':')-url-i);
	   /*
       strncpy()用来复制字符串的前n个字符，其原型为：
       char * strncpy(char *dest, const char *src, size_t n);
      【参数说明】dest 为目标字符串指针，src 为源字符串指针。
       strncpy()会将字符串src前n个字符拷贝到字符串dest。
	   */
	   bzero(tmp,10);
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
	   if(proxyport==0) proxyport=80;
   } else
   {
     strncpy(host,url+i,strcspn(url+i,"/"));//strcspn size_t strcspn(const char *s, const char *reject);

         //该函数对字符串reject中的每个字符在s中查找，
	 //是否存在，如果有超过一个以上的字符在s中存在，
	 //那么返回这些字符位置(在s中的位置)中最小的一个。
   }
   // printf("Host=%s\n",host);
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   strcat(request,url);
  }
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
  int i,j,k;	
  pid_t pid=0;
  FILE *f;

  /* check avaibility of target server */ //检查目标服务器的可用性
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  close(i);
  /* create pipe */
  if(pipe(mypipe)) //int pipe(int filedses[2]) 创建两个进程互相通信的管道 
  //参数数组包含pipe使用的两个文件描述符 fd[0]读 管道 fd[1]写管道
  // 成功返回0 否则返回-1
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   if(pid <= (pid_t) 0) //pid=0 当前进程为子进程 pid<0 创建进程失败  直接跳出for循环	
   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
		   break;
	   }
  }

  if( pid< (pid_t) 0)  //创建进程出错 打印出错信息并结束函数
  {
          fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0) //当前进程是子进程
  {
    /* I am a child */
    if(proxyhost==NULL)  //若代理服务器端口为空
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

         /* write results to pipe */
	 f=fdopen(mypipe[1],"w");//将结果写入管道 
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
	 fprintf(f,"%d %d %d\n",speed,failed,bytes);//把	每个子进程运行的结果放入管道
	 fclose(f);
	 return 0;
  } else    //当前进程是父进程
  {
	  f=fdopen(mypipe[0],"r");  
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);
	  speed=0;
          failed=0;
          bytes=0;

	  while(1)//父进程读取管道数据，并做加法 
	  {
		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		  if(--clients==0) break;
	  }
	  fclose(f);

  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  }
  return i;
}

void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500];
 int s,i;
 struct sigaction sa;

 /* setup alarm signal handler */
 sa.sa_handler=alarm_handler;//定时器方法
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);
 alarm(benchtime);

 rlen=strlen(req);
 nexttry:while(1)
 {
    if(timerexpired)   //定时器到时后，会设定timerexpired=1，函数就会返回  
    {
       if(failed>0)
       {
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;
       }
       return;
    }
    s=Socket(host,port);       //创建连接                   
    if(s<0) { failed++;continue;}  //连接失败 纪录失败的计数器加1
    /*
    定义函数：ssize_t write (int fd, const void * buf, size_t count);
    函数说明：write()会把参数buf 所指的内存写入count 个字节到参数fd 所指的文件内. 
    当然, 文件读写位置也会随之移动
    返回值：如果顺利write()会返回实际写入的字节数. 
    当有错误发生时则返回-1, 错误代码存入errno 中.
    */
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}  
    /*
    定义函数：int close(int fd);
    函数说明：当使用完文件后若已不再需要则可使用 close()关闭该文件, 
    close()会让数据写回磁盘, 并释放该文件所占用的资源.
    参数fd 为先前由open()或creat()所返回的文件描述词.
    返回值：若文件顺利关闭则返回0, 发生错误时返回-1.
    */
    if(http10==0)
    /*
   定义函数：int shutdown(int s, int how);

   函数说明：shutdown()用来终止参数s 所指定的socket 连线. 参数s 是连线中的socket 处理代码, 参数how有下列几种情况:
   how=0 终止读取操作.
   how=1 终止传送操作
   how=2 终止读取及传送操作

   返回值：成功则返回0, 失败返回-1, 错误原因存于errno
    */
	    if(shutdown(s,1)) { failed++;close(s);continue;}//终止s所指定的socket连接传送操作失败 连接失败计数器+1
    if(force==0) 
    {
            /*读取套接字所有可用的数据*/ 
	    while(1)
	    {
              if(timerexpired) break;  //定时器到时后，会设定timerexpired=1，函数就会返回  
	      i=read(s,buf,1500);//从s所指向的文件描述符里读取1500个字节到buf所指向的内存空间中去
              /* fprintf(stderr,"%d\n",i); */
	      if(i<0) 
              { 
                 failed++;
                 close(s);
                 goto nexttry;
              }
	       else
		       if(i==0) break;
		       else
			       bytes+=i;
	    }
    }
    if(close(s)) {failed++;continue;}//关闭s所指向的连接失败，失败次数 加1
    speed++;
 }
}
