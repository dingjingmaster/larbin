// Larbin
// Sebastien Ailleret
// 29-11-99 -> 09-03-02

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>
#include <adns.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include "options.h"

#include "types.h"
#include "global.h"
#include "utils/text.h"
#include "utils/Fifo.h"
#include "utils/debug.h"
#include "fetch/site.h"
#include "interf/output.h"
#include "interf/input.h"


///////////////////////////////////////////////////////////
// Struct global
///////////////////////////////////////////////////////////

// define all the static variables
time_t global::now;
hashTable *global::seen;
#ifdef NO_DUP
hashDup *global::hDuplicate;
#endif // NO_DUP
SyncFifo<url> *global::URLsPriority;                                                // 存放待处理的url列表,优先级高
SyncFifo<url> *global::URLsPriorityWait;                                            // 当前正处理url超过上限,存放待处理的url列表,优先级高
uint global::readPriorityWait=0;                                                    // 当前待处理url列表记录数
PersistentFifo *global::URLsDisk;                                                   // 存放待处理的url
PersistentFifo *global::URLsDiskWait;                                               // 当前正处理的url超过上限,存放待处理url列表
uint global::readWait=0;                                                            // 当前 URLDiskWait 记录数
IPSite *global::IPSiteList;
NamedSite *global::namedSiteList;                                                   // 存放域名及域名解析状态,用来排重和定位
Fifo<IPSite> *global::okSites;                                                      // 存放经过dns解析,待爬取的数据
Fifo<NamedSite> *global::dnsSites;
Connexion *global::connexions;                                                      // 连接对象,绑定当前状态及响应的处理器
adns_state global::ads;
uint global::nbDnsCalls = 0;
ConstantSizedFifo<Connexion> *global::freeConns;
#ifdef THREAD_OUTPUT
ConstantSizedFifo<Connexion> *global::userConns;                                    //
#endif
Interval *global::inter;
int8_t global::depthInSite;
bool global::externalLinks = true;
time_t global::waitDuration;
char *global::userAgent;
char *global::sender;
char *global::headers;
char *global::headersRobots;
sockaddr_in *global::proxyAddr;
Vector<char> *global::domains;
Vector<char> global::forbExt;
uint global::nb_conn;
uint global::dnsConn;
unsigned short int global::httpPort;
unsigned short int global::inputPort;
struct pollfd *global::pollfds;
uint global::posPoll;
uint global::sizePoll;
short *global::ansPoll;
int global::maxFds;
#ifdef MAXBANDWIDTH
long int global::remainBand = MAXBANDWIDTH;                                         // 带宽
#endif // MAXBANDWIDTH
int global::IPUrl = 0;

/** Constructor : initialize almost everything
 * Everything is read from the config file (larbin.conf by default)
 */
global::global (int argc, char *argv[]) {
  char *configFile = "larbin.conf";
#ifdef RELOAD
  bool reload = true;
#else
  bool reload = false;
#endif
  now = time(NULL);
  // verification of arguments
  int pos = 1;
  while (pos < argc) {
	if (!strcmp(argv[pos], "-c") && argc > pos+1) {
	  configFile = argv[pos+1];
	  pos += 2;
	} else if (!strcmp(argv[pos], "-scratch")) {
	  reload = false;
	  pos++;
	} else {
	  break;
	}
  }
  if (pos != argc) {
      std::cerr << "usage : " << argv[0];
      std::cerr << " [-c configFile] [-scratch]\n";
	exit(1);
  }                                                                                 // 默认配置文件

  // Standard values
  waitDuration = 60;
  depthInSite = 5;
  userAgent = "larbin";
  sender = "larbin@unspecified.mail";
  nb_conn = 20;
  dnsConn = 3;
  httpPort = 0;
  inputPort = 0;  // by default, no input available
  proxyAddr = NULL;
  domains = NULL;
  // FIFOs
  URLsDisk = new PersistentFifo(reload, fifoFile);                                  // 低优先的url列表
  URLsDiskWait = new PersistentFifo(reload, fifoFileWait);                          // 低优先url列表满了之后暂存
  URLsPriority = new SyncFifo<url>;                                                 // 高优先url列表
  URLsPriorityWait = new SyncFifo<url>;                                             // 高优先url列表满了之后暂存
  inter = new Interval(ramUrls);
  namedSiteList = new NamedSite[namedSiteListSize];                                 // 域名及域名解析状态
  IPSiteList = new IPSite[IPSiteListSize];
  okSites = new Fifo<IPSite>(2000);                                                 // 经过dns解析带爬取的数据
  dnsSites = new Fifo<NamedSite>(2000);
  seen = new hashTable(!reload);
#ifdef NO_DUP
  hDuplicate = new hashDup(dupSize, dupFile, !reload);
#endif // NO_DUP
  // Read the configuration file
  crash("Read the configuration file");
  parseFile(configFile);
  // Initialize everything
  crash("Create global values");
  // Headers
  LarbinString strtmp;
  strtmp.addString("\r\nUser-Agent: ");
  strtmp.addString(userAgent);
  strtmp.addString(" ");
  strtmp.addString(sender);
#ifdef SPECIFICSEARCH
  strtmp.addString("\r\nAccept: text/html");                                        //
  int i=0;
  while (contentTypes[i] != NULL) {
    strtmp.addString(", ");
    strtmp.addString(contentTypes[i]);
    i++;
  }
#elif !defined(IMAGES) && !defined(ANYTYPE)
  strtmp.addString("\r\nAccept: text/html");
#endif // SPECIFICSEARCH
  strtmp.addString("\r\n\r\n");
  headers = strtmp.giveString();
  // Headers robots.txt
  strtmp.recycle();
  strtmp.addString("\r\nUser-Agent: ");
  strtmp.addString(userAgent);
  strtmp.addString(" (");
  strtmp.addString(sender);
  strtmp.addString(")\r\n\r\n");                                                    // 请求头
  headersRobots = strtmp.giveString();
#ifdef THREAD_OUTPUT
  userConns = new ConstantSizedFifo<Connexion>(nb_conn);                            //
#endif
  freeConns = new ConstantSizedFifo<Connexion>(nb_conn);
  connexions = new Connexion [nb_conn];
  for (uint i = 0; i < nb_conn; ++ i) {
	freeConns->put(connexions+i);
  }
  // init poll structures
  sizePoll = nb_conn + maxInput;                                                    // 初始化poll
  pollfds = new struct pollfd[sizePoll];
  posPoll = 0;
  maxFds = sizePoll;
  ansPoll = new short[maxFds];                                                      // short 2^15
  // init non blocking dns calls
  adns_initflags flags =
	adns_initflags (adns_if_nosigpipe | adns_if_noerrprint);                        // dns 初始化
  adns_init(&ads, flags, NULL);
  // call init functions of all modules
  initSpecific();
  initInput();
  initOutput();
  initSite();
  // let's ignore SIGPIPE
  static struct sigaction sn, so;                                                   // 忽略管道破裂的错误
  sigemptyset(&sn.sa_mask);
  sn.sa_flags = SA_RESTART;
  sn.sa_handler = SIG_IGN;
  if (sigaction(SIGPIPE, &sn, &so)) {                                               // 捕获信号
      std::cerr << "Unable to disable SIGPIPE : " << strerror(errno) << std::endl;
  }
}

/** Destructor : never used because the program should never end !
 */
global::~global () {                                                                // 析构函数
  assert(false);
}

/** parse configuration file */
void global::parseFile (char *file) {                                               // 读取配置文件并解析
  int fds = open(file, O_RDONLY);
  if (fds < 0) {
      std::cerr << "cannot open config file (" << file << ") : "
         << strerror(errno) << std::endl;
	exit(1);
  }
  char *tmp = readfile(fds);
  close(fds);
  // suppress commentary
  bool eff = false;                                                                 // 配置文件注释行不读入
  for (int i = 0; tmp[i] != 0; ++ i) {                                              // 删除某行"#"及其后的所有内容
	switch (tmp[i]) {
	case '\n': eff = false; break;                                                  //
	case '#': eff = true;                                                           // 
	default: if (eff) tmp[i] = ' ';
	}
  }
  char *posParse = tmp;                                                             // 解析并应用配置文件
  char *tok = nextToken(&posParse);
  while (tok != NULL) {                                                             
	if (!strcasecmp(tok, "UserAgent")) {
	  userAgent = newString(nextToken(&posParse));
	} else if (!strcasecmp(tok, "From")) {
	  sender = newString(nextToken(&posParse));
	} else if (!strcasecmp(tok, "startUrl")) {
	  tok = nextToken(&posParse);
      url *u = new url(tok, global::depthInSite, (url *) NULL);
      if (u->isValid()) {
        check(u);
      } else {
          std::cerr << "the start url " << tok << " is invalid\n";
        exit(1);
      }
	} else if (!strcasecmp(tok, "waitduration")) {
	  tok = nextToken(&posParse);
	  waitDuration = atoi(tok);
	} else if (!strcasecmp(tok, "proxy")) {
	  // host name and dns call
	  tok = nextToken(&posParse);
	  struct hostent* hp;
	  proxyAddr = new sockaddr_in;
	  memset((char *) proxyAddr, 0, sizeof (struct sockaddr_in));
	  if ((hp = gethostbyname(tok)) == NULL) {
		endhostent();
        std::cerr << "Unable to find proxy ip address (" << tok << ")\n";
		exit(1);
	  } else {
		proxyAddr->sin_family = hp->h_addrtype;
		memcpy ((char*) &proxyAddr->sin_addr, hp->h_addr, hp->h_length);
	  }
	  endhostent();                                                                 //
	  // port number
	  tok = nextToken(&posParse);
	  proxyAddr->sin_port = htons(atoi(tok));
	} else if (!strcasecmp(tok, "pagesConnexions")) {
	  tok = nextToken(&posParse);
	  nb_conn = atoi(tok);
	} else if (!strcasecmp(tok, "dnsConnexions")) {
	  tok = nextToken(&posParse);
	  dnsConn = atoi(tok);
	} else if (!strcasecmp(tok, "httpPort")) {
	  tok = nextToken(&posParse);
	  httpPort = atoi(tok);
	} else if (!strcasecmp(tok, "inputPort")) {
	  tok = nextToken(&posParse);
	  inputPort = atoi(tok);
	} else if (!strcasecmp(tok, "depthInSite")) {
	  tok = nextToken(&posParse);
	  depthInSite = atoi(tok);
	} else if (!strcasecmp(tok, "limitToDomain")) {
	  manageDomain(&posParse);
	} else if (!strcasecmp(tok, "forbiddenExtensions")) {
	  manageExt(&posParse);
	} else if (!strcasecmp(tok, "noExternalLinks")) {
	  externalLinks = false;
	} else {
        std::cerr << "bad configuration file : " << tok << "\n";
	  exit(1);
	}
	tok = nextToken(&posParse);                                                     //
  }
  delete [] tmp;
}

/** read the domain limit */
void global::manageDomain (char **posParse) {                                       // 域名管理
  char *tok = nextToken(posParse);
  if (domains == NULL) {
	domains = new Vector<char>;
  }
  while (tok != NULL && strcasecmp(tok, "end")) {
	domains->addElement(newString(tok));
	tok = nextToken(posParse);
  }
  if (tok == NULL) {
      std::cerr << "Bad configuration file : no end to limitToDomain\n";
	exit(1);
  }
}

/** read the forbidden extensions */
void global::manageExt (char **posParse) {                                          // 
  char *tok = nextToken(posParse);
  while (tok != NULL && strcasecmp(tok, "end")) {
    int l = strlen(tok);
    int i;
    for (i=0; i<l; i++) {
      tok[i] = tolower(tok[i]);
    }
    if (!matchPrivExt(tok))
      forbExt.addElement(newString(tok));
	tok = nextToken(posParse);
  }
  if (tok == NULL) {
      std::cerr << "Bad configuration file : no end to forbiddenExtensions\n";
	exit(1);
  }
}

/** make sure the max fds has not been reached */
void global::verifMax (int fd) {                                                    // 当fd达到最大数量时,增加
  if (fd >= maxFds) {
    int n = 2 * maxFds;
    if (fd >= n) {
      n = fd + maxFds;
    }
    short *tmp = new short[n];
    for (int i = 0; i < maxFds; ++ i) {
      tmp[i] = ansPoll[i];
    }
    for (int i = maxFds; i < n; ++ i) {
      tmp[i] = 0;
    }
    delete (ansPoll);
    maxFds = n;
    ansPoll = tmp;
  }
}

///////////////////////////////////////////////////////////
// Struct Connexion
///////////////////////////////////////////////////////////

/** put Connection in a coherent state
 */
Connexion::Connexion () {                                                           // 改变conne 状态
  state = emptyC;
  parser = NULL;
}

/** Destructor : never used : we recycle !!!
 */
Connexion::~Connexion () {
  assert(false);
}

/** Recycle a connexion
 */
void Connexion::recycle () {                                                        // 回收一个连接
  delete parser;
  request.recycle();
}
