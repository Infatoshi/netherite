/* Linux-only, explicit-identity process hold. Dry-run by default.
 * Guardian survives frontend death. pidfds prevent PID-reuse signals.
 * Root trees are snapshotted, then stopped; this is not a cgroup freezer.
 * Uncatchable guardian death or host failure cannot be recovered in-process.
 */
#define _GNU_SOURCE
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <poll.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#define MAX 4096
struct proc {pid_t pid,ppid; unsigned long long start; char state; int fd,held,root;};
static struct proc ps[MAX]; static int count; static volatile sig_atomic_t stop;
static int guard_parent=-1; static double guard_deadline;
static void handler(int s){(void)s;stop=1;}
static double clock_s(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int cancelled(void){if(stop)return 1;if(guard_parent<0)return 0;struct pollfd p={guard_parent,POLLIN,0};poll(&p,1,0);return (p.revents&(POLLIN|POLLERR|POLLNVAL))||clock_s()>=guard_deadline;}
static int stat_pid(pid_t pid,struct proc *p){char path[80],buf[8192];snprintf(path,sizeof path,"/proc/%d/stat",pid);FILE*f=fopen(path,"r");if(!f)return -1;if(!fgets(buf,sizeof buf,f)){fclose(f);return -1;}fclose(f);char *s=strrchr(buf,')'),*save;if(!s)return -1;memset(p,0,sizeof *p);p->pid=pid;p->fd=-1;int field=3;for(char*t=strtok_r(s+2," ",&save);t;t=strtok_r(NULL," ",&save),field++){if(field==3)p->state=*t;if(field==4)p->ppid=atoi(t);if(field==22){p->start=strtoull(t,NULL,10);return 0;}}return -1;}
static int known(pid_t pid){for(int i=0;i<count;i++)if(ps[i].pid==pid)return i;return -1;}
static int add(struct proc p){if(count==MAX){errno=E2BIG;return -1;}int fd=syscall(SYS_pidfd_open,p.pid,0);if(fd<0)return -1;struct proc check;if(stat_pid(p.pid,&check)||check.start!=p.start||(!p.root&&check.ppid!=p.ppid)){close(fd);errno=ESRCH;return -1;}p.fd=fd;ps[count++]=p;return 0;}
static int signal_p(struct proc*p,int sig){return syscall(SYS_pidfd_send_signal,p->fd,sig,NULL,0);}
static int collect(void){int added;do{added=0;DIR*d=opendir("/proc");if(!d)return -1;struct dirent*de;while((de=readdir(d))){if(cancelled()){closedir(d);errno=ECANCELED;return -1;}char*end;long id=strtol(de->d_name,&end,10);if(*end||id<2||known(id)>=0)continue;struct proc p;if(stat_pid(id,&p))continue;int parent=known(p.ppid);if(parent<0)continue;struct proc live;struct pollfd pinned={ps[parent].fd,POLLIN,0};poll(&pinned,1,0);if(pinned.revents||stat_pid(p.ppid,&live)||live.start!=ps[parent].start)continue;if(add(p)){if(errno==ESRCH||errno==ENOENT)continue;closedir(d);return -1;}added++;}closedir(d);}while(added);return 0;}
static void resume(void){for(int i=count-1;i>=0;i--)if(ps[i].held){int rc=signal_p(&ps[i],SIGCONT);fprintf(stderr,"RESUME pid=%d start=%llu rc=%d errno=%d\n",ps[i].pid,ps[i].start,rc,rc?errno:0);ps[i].held=0;}}
static int hold(void){for(int i=0;i<count;i++){if(cancelled())return -1;if(ps[i].held)continue;struct proc p;if(stat_pid(ps[i].pid,&p)||p.start!=ps[i].start){if(ps[i].root)return -1;continue;}struct pollfd exited={ps[i].fd,POLLIN,0};poll(&exited,1,0);if(exited.revents||p.state=='Z'||p.state=='X'){if(ps[i].root)return -1;continue;}if(p.state=='T'||p.state=='t'){fprintf(stderr,"KEEP_STOPPED pid=%d\n",p.pid);continue;}if(signal_p(&ps[i],SIGSTOP)){if(errno==ESRCH&&!ps[i].root)continue;return -1;}ps[i].held=1;fprintf(stderr,"STOP pid=%d start=%llu\n",p.pid,p.start);}return 0;}
static void usage(void){fprintf(stderr,"usage: hold_jobs --root PID:START_TICKS [--root ...] --seconds 1..2700 [--execute] -- COMMAND [ARGS...]\nDry-run prints exact captured process tree. Execution requires external user approval.\n");}
int main(int argc,char**argv){int execute=0,seconds=0,arg=1;for(;arg<argc;arg++){if(!strcmp(argv[arg],"--")){arg++;break;}if(!strcmp(argv[arg],"--execute")){execute=1;continue;}if(arg+1>=argc){usage();return 2;}char*k=argv[arg++],*v=argv[arg];if(!strcmp(k,"--seconds")){char*end;long x=strtol(v,&end,10);if(*end||x<1||x>2700){usage();return 2;}seconds=x;}else if(!strcmp(k,"--root")){int pid;unsigned long long start;char tail;if(sscanf(v,"%d:%llu%c",&pid,&start,&tail)!=2||pid<2||pid==getpid()||known(pid)>=0){usage();return 2;}struct proc p;if(stat_pid(pid,&p)||p.start!=start){fprintf(stderr,"identity mismatch: %s\n",v);return 2;}p.root=1;if(add(p)){perror("pin root");return 2;}}else{usage();return 2;}}
 if(!count||!seconds||arg>=argc){usage();return 2;}if(collect()){perror("collect");return 2;}if(known(getpid())>=0){fprintf(stderr,"refuse to hold own ancestor\n");return 2;}
 for(int i=0;i<count;i++){printf("PLAN pid=%d ppid=%d start=%llu prior_state=%c root=%d\n",ps[i].pid,ps[i].ppid,ps[i].start,ps[i].state,ps[i].root);}
 printf("PLAN seconds=%d execute=%d command_argv_count=%d\n",seconds,execute,argc-arg);for(int i=arg;i<argc;i++)printf("ARG %d %s\n",i-arg,argv[i]);fflush(stdout);if(!execute)return 0;
 int parentfd=syscall(SYS_pidfd_open,getpid(),0);if(parentfd<0){perror("parent pidfd");return 2;}pid_t guardian=fork();if(guardian<0){perror("fork guardian");return 2;}if(guardian){signal(SIGINT,handler);signal(SIGTERM,handler);signal(SIGHUP,handler);int status;for(;;){if(stop)_exit(130);pid_t r=waitpid(guardian,&status,WNOHANG);if(r==0){usleep(10000);continue;}if(r==guardian)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);if(errno==EINTR){if(stop)_exit(130);continue;}return 2;}}
 if(setsid()<0){_exit(2);}
 signal(SIGINT,handler);signal(SIGTERM,handler);signal(SIGHUP,handler);signal(SIGPIPE,SIG_IGN);double deadline=clock_s()+seconds;guard_parent=parentfd;guard_deadline=deadline;pid_t command=-1;int result=2;
 fprintf(stderr,"GUARDIAN pid=%d parent_fd=%d deadline_seconds=%d\n",getpid(),parentfd,seconds);
 /* Confirm STOP before each new descendant closure. Once all pinned parents
  * are stopped, a stable closure cannot create another child without an
  * external actor resuming it. Cancellation always rolls back held entries. */
 for(;;){
  if(cancelled()||hold())goto done;
  for(int i=0;i<count;i++)if(ps[i].held){
   for(;;){
    if(cancelled())goto done;
    struct proc p;if(stat_pid(ps[i].pid,&p)||p.start!=ps[i].start){if(ps[i].root)goto done;break;}
    struct pollfd exited={ps[i].fd,POLLIN,0};poll(&exited,1,0);if(exited.revents||p.state=='Z'||p.state=='X'){if(ps[i].root)goto done;break;}
    if(p.state=='T'||p.state=='t')break;
    usleep(10000);
   }
  }
  int old=count;if(collect()||cancelled())goto done;
  if(count==old)break;
 }
 if(cancelled())goto done;
 command=fork();if(command<0)goto done;if(!command){setpgid(0,0);for(int i=0;i<count;i++)close(ps[i].fd);close(parentfd);signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);execvp(argv[arg],argv+arg);_exit(127);}setpgid(command,command);
 for(;;){siginfo_t info={0};if(waitid(P_PID,command,&info,WEXITED|WNOHANG|WNOWAIT)==0&&info.si_pid){result=info.si_code==CLD_EXITED?info.si_status:128+info.si_status;break;}struct pollfd p={parentfd,POLLIN,0};poll(&p,1,50);if(stop||(p.revents&POLLIN)||clock_s()>=deadline){result=124;break;}}
 done:
 /* Resume first, even if the measurement command refuses termination. */
 resume();if(command>0){kill(-command,SIGTERM);double end=clock_s()+1;int status;while(clock_s()<end)usleep(10000);/* Leader remains unreaped: its process-group ID cannot be reused. */kill(-command,SIGKILL);waitpid(command,&status,0);}fprintf(stderr,"GUARDIAN_EXIT rc=%d\n",result);_exit(result);
}
