#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

#define PATH_LEN 4096
#define NAME_LEN 4096
#define THREAD_MAX 16
#define LIST_FULL  50000
//#define LIST_FULL  3

char g_name[1024];

#define container_of(ptr, type, member) ({                              \
                        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
                        (type *)( (char *)__mptr - offsetof(type,member) );})

#define list_entry(ptr, type, member)           \
        container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
        list_entry((ptr)->next, type, member)

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

static inline void __list_add(
        struct list_head *list, struct list_head *prev,
        struct list_head *next)
{
	next->prev = list;
	list->next = next;
	list->prev = prev;
	prev->next = list;
}

static inline void list_add(
        struct list_head *list, struct list_head *head)
{
	__list_add(list, head, head->next);
}

static inline void list_add_tail(
        struct list_head *list, struct list_head *head)
{
    __list_add(list, head->prev, head);
}

static inline void list_del(struct list_head *entry)
{
	struct list_head *prev = entry->prev;
	struct list_head *next = entry->next;

	next->prev = prev;
	prev->next = next;
}

static inline void list_del_init(struct list_head *entry)
{
    list_del(entry);
    INIT_LIST_HEAD(entry);
}

#define prefetch(x) __builtin_prefetch(x)
#define list_for_each(pos, head) \
    for (pos = (head)->next; prefetch(pos->next), pos != (head); \
        pos = pos->next)
#define __list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

int strnmatch(const char *str1, const char *str2)
{
    size_t size = strlen(str2);
    if (strlen(str1) != size)
        return 0;

    return (strncmp(str1, str2, size) == 0); 
}

struct dir_point {
    DIR *d;
    struct dir_point *parent;
    struct list_head link;
    struct list_head subdirs;
    char path[PATH_LEN];
    int subfile;
};
LIST_HEAD(dp_list);

/**
struct fff {
    struct list_head link;
    char path[PATH_LEN];
};
LIST_HEAD(file_list);
**/

struct dir_entry {
    struct list_head link;
    char name[NAME_LEN];
};

pthread_t           tid[THREAD_MAX] = { 0 };
char                dir[PATH_LEN];
int                 running         = 1;
int                 threadnum       = 0;
int                 workthread      = 0;

long long           node_count      = 1;

unsigned long long  sfilenum        = 0;
unsigned long long  sfilesize       = 0;
unsigned long long  dirnum          = 0;

pthread_mutex_t dplistmutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t nummutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t node_count_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  startcond    = PTHREAD_COND_INITIALIZER;
pthread_cond_t  endcond      = PTHREAD_COND_INITIALIZER;
pthread_cond_t  list_not_full_cond      = PTHREAD_COND_INITIALIZER;

struct dir_point* dp;
struct dir_point* transdp;
struct dir_entry* de;

void show_usage()
{
    printf("Usage: lsdf directory threadnum\n");
}

struct dir_point *alloc_dp(
        struct dir_point *parent, char *path)
{
    struct dir_point *dp = NULL;

    dp = malloc(sizeof(struct dir_point));
    if (!dp) {
        return NULL;
    }


    dp->d = NULL;
    dp->subfile = 0;
    dp->parent = parent;

    INIT_LIST_HEAD(&dp->subdirs);
    INIT_LIST_HEAD(&dp->link);
    list_add(&dp->link, &dp_list);
    snprintf(dp->path, PATH_LEN, "%s", path);

    return dp;
}

struct dir_entry *alloc_de(struct dir_point *dp, char *name)
{
    struct dir_entry *de = NULL;

    de = malloc(sizeof(struct dir_entry));
    if (!de) {
        return NULL;
    }

    INIT_LIST_HEAD(&de->link);
    list_add(&de->link, &dp->subdirs);
    snprintf(de->name, NAME_LEN, "%s", name);

    return de;
}

int deep_scan_dir(struct dir_point *dp, char *path)
{
    struct dir_point *ldp = NULL;
    struct dir_point *locdp = NULL;
    struct dir_point *tmpdp = NULL;
    struct dir_point *deepdp = NULL;
    char             locpath[PATH_LEN];
    int              ret = 0;
    struct stat      st;
    int              finish = 0;
    struct list_head *pos, *pos1;

    ldp = malloc(sizeof(struct dir_point));
    if (!dp) {
        return -1;
    }

    ldp->d = opendir(path);
    if (ldp->d == NULL) {
        free(ldp);
        return -1;
    }
    ldp->subfile = 0;
    ldp->parent = dp;

    INIT_LIST_HEAD(&ldp->subdirs);
    INIT_LIST_HEAD(&ldp->link);
    list_add(&ldp->link, &dp->subdirs);
    snprintf(ldp->path, PATH_LEN, "%s", path);

    ret = lstat(path, &st);
    if (ret < 0) {
        return -1;
    }

    pthread_mutex_lock(&nummutex);
    sfilesize += st.st_size;
    dirnum++;
    pthread_mutex_unlock(&nummutex);

    while (!list_empty(&dp->subdirs)) {
        locdp = list_entry(dp->subdirs.next, struct dir_point, link);

        tmpdp = locdp;
        finish = 0;
        while (1) {
            struct dirent ent, *ptr;

            ret = readdir_r(tmpdp->d, &ent, &ptr);
            if (ret < 0) {
                break;
            }
            if (!ptr) {
                finish = 0;
                break;
            }

            if (strnmatch(ent.d_name, (char *)".") ||
                strnmatch(ent.d_name, (char *)"..") ||
                strnmatch(ent.d_name, (char *)"")) {
                /*if (strnmatch(ent.d_name, (char *)"")) printf("ent.d_name:%s\n", ent.d_name);*/
                continue;
            }

            tmpdp->subfile++;

            snprintf( locpath, PATH_LEN, "%s/%s", tmpdp->path, ent.d_name );
            ret = lstat(locpath, &st);
            if (ret < 0) {
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                snprintf( locpath, PATH_LEN, "%s/%s", 
                        tmpdp->path, ent.d_name );

                pthread_mutex_lock(&dplistmutex);
                if (node_count < LIST_FULL) {
                    if (alloc_dp(dp, locpath) == NULL) {
                        pthread_mutex_unlock(&dplistmutex);
                        continue;
                    }
                    node_count += 1;
                    //printf("%0x %s add to global list..%lld\n", pthread_self(), path, node_count);
                    pthread_cond_signal(&startcond);
                    pthread_mutex_unlock(&dplistmutex);
                    continue;
                }
                pthread_mutex_unlock(&dplistmutex);

                deepdp = malloc(sizeof(struct dir_point));
                if (deepdp == NULL) {
                    continue;
                }

                deepdp->d = opendir(locpath);
                if (deepdp->d == NULL) {
                    free(deepdp);
                    continue;
                }
                deepdp->subfile = 0;
                deepdp->parent = dp;

                INIT_LIST_HEAD(&deepdp->subdirs);
                INIT_LIST_HEAD(&deepdp->link);
                snprintf(deepdp->path, PATH_LEN, "%s", locpath);
                list_add(&deepdp->link, &dp->subdirs);
                tmpdp = deepdp;

                pthread_mutex_lock(&nummutex);
                dirnum++;
                sfilesize += st.st_size;
                pthread_mutex_unlock(&nummutex);

                continue;
            }

            pthread_mutex_lock(&nummutex);
            sfilenum++;
            /**
            if (sfilenum % 30000 == 0)
                printf("%0x deep scan :%lld\n", pthread_self(), sfilenum);
            **/
            sfilesize += st.st_size;
            pthread_mutex_unlock(&nummutex);
        }
        closedir(tmpdp->d);
        locdp = list_first_entry(&dp->subdirs, struct dir_point, link);
        list_del((dp->subdirs.next));
        free(locdp);
    }

    return 0;
}

void* thread_routine(void* args)
{
    int                ret = 0;
    char               path[PATH_LEN];
    struct stat        st;
    struct dir_point*  locdp = NULL;
    struct dir_point*  extdp = NULL;
    struct dir_point*  tmpdp = NULL;

    struct timeval btv;
    struct timeval etv;
    struct list_head *pos, *pos1;

    while (1) {

        pthread_mutex_lock(&dplistmutex);
        while (list_empty(&dp_list) && running != 0) {
            pthread_cond_wait(&startcond, &dplistmutex);
        }

        if (running == 0) {
            pthread_mutex_unlock(&dplistmutex);
            //continue;
            break;
        }

        locdp = list_entry(dp_list.next, 
                struct dir_point, link);
        list_del((dp_list.next));

        workthread++;
        pthread_mutex_unlock(&dplistmutex);

        if (!locdp->d) {
            locdp->d = opendir(locdp->path);
            if (!locdp->d) {
                pthread_mutex_lock(&dplistmutex);
                workthread--;
                pthread_cond_signal(&endcond);
                pthread_mutex_unlock(&dplistmutex);

                free(locdp);
                continue;
            }

            ret = lstat(locdp->path, &st);
            if (ret < 0) {
                free(locdp);
                continue;
            }

            pthread_mutex_lock(&nummutex);
            dirnum++;
            sfilesize += st.st_size;
            pthread_mutex_unlock(&nummutex);

            tmpdp = locdp;
            while (1) {
                struct dirent ent, *ptr;

                ret = readdir_r(tmpdp->d, &ent, &ptr);
                if (ret < 0) {
                    break;
                }

                if (!ptr) {
                    /* Finish */
                    break;
                }

                if (strnmatch(ent.d_name, (char *)".") ||
                        strnmatch(ent.d_name, (char *)"..")||
                        strnmatch(ent.d_name, (char *)"")) {
                       /*if (strnmatch(ent.d_name, (char *)"")) printf("ent.d_name:%s\n", ent.d_name);*/
                    continue;
                }

                tmpdp->subfile++; 

                snprintf(path, PATH_LEN, "%s/%s", 
                        tmpdp->path, ent.d_name);

                ret = lstat(path, &st);
                if (ret < 0) {
                    continue;
                }

                if (S_ISDIR(st.st_mode)) {
                    snprintf( path, PATH_LEN, "%s/%s", 
                            tmpdp->path, ent.d_name );

                    pthread_mutex_lock(&dplistmutex);
                    node_count += 1;
                    if (node_count >= LIST_FULL) {
                        node_count -= 1;
                        pthread_mutex_unlock(&dplistmutex);
                        //printf("goto deep_scan.%0x\n", pthread_self());
                        goto deep_scan;
                    }
                    else if (alloc_dp(locdp, path) == NULL) {
                        pthread_cond_signal(&startcond);
                        pthread_mutex_unlock(&dplistmutex);
                        continue;
                    }

                    pthread_cond_signal(&startcond);
                    pthread_mutex_unlock(&dplistmutex);

                    continue;
                }

                pthread_mutex_lock(&nummutex);
                sfilenum++;
                sfilesize += st.st_size;
                /**
                if (sfilenum % 30000 == 0) {
                    printf("Scan files number:%ld\n", sfilenum);
                }
                **/
                pthread_mutex_unlock(&nummutex);
                continue;
deep_scan:
                deep_scan_dir(tmpdp, path);

                /**
                pthread_mutex_lock(&dplistmutex);
                node_count -= 1;
                printf("after thread deep_scan_dir min node_count:%lld\n", node_count);
                pthread_mutex_unlock(&dplistmutex);
                **/
            }
        }
        closedir(locdp->d);

        pthread_mutex_lock(&dplistmutex);
        node_count -= 1;
        //printf("thread 1 min node_count:%lld\n", node_count);
        workthread--;
        if (list_empty(&dp_list) && workthread == 0) {
            pthread_cond_signal(&endcond);
        }
        pthread_mutex_unlock(&dplistmutex);

        free(locdp);
    }

    return (void*)0;
}

int create_thread( )
{
    int ret = 0;
    int i   = 0;
    int j   = 0;

    for (i = 0; i < threadnum; i++) {
        ret = pthread_create(&tid[i], NULL, 
                thread_routine, NULL);
        if (ret < 0) {
            running = 0;

            for (j = 0; j < i; j++) {

                ret = pthread_join(tid[j], NULL);
                if (ret < 0) {
                    return errno;
                }
            }
            return errno;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int          ret = 0;
    int          j   = 0;
    struct stat  st;
    int          c = 0;
    char         *short_opts = "sb";
    int          bf = -1;
    int          sf = -1;

    //struct timeval btv;
    //struct timeval etv;

    if (argc != 3) {
        fprintf(stdout, "./lsdf dir filename\n");
        return -1;
    }

    strcpy(g_name, argv[2]);

    memset(dir, PATH_LEN, 0);

    snprintf(dir, PATH_LEN, "%s", argv[1]);

    threadnum = THREAD_MAX;
    /**
    if (threadnum > THREAD_MAX || threadnum < 1) {
        printf("thread number: 1~128\n");
        return -1;
    }
    **/

    ret = lstat(dir, &st);
    if (ret < 0) {
        fprintf(stdout, "-%d", errno);
        return errno;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stdout, "%d", -1);
        return -1;
    }

    //gettimeofday(&btv, NULL);

    ret = create_thread();
    if (ret < 0) {
        fprintf(stdout, "%d", -1);
        return -1;;
    }

    pthread_mutex_lock(&dplistmutex);
    dp = alloc_dp(NULL, dir);
    if (!dp) {
        fprintf(stdout, "%d", -1);
        return -1;
    }
    pthread_mutex_unlock(&dplistmutex);

    pthread_cond_signal(&startcond);

    pthread_mutex_lock(&dplistmutex);
    while (!list_empty(&dp_list) || workthread != 0) {
        pthread_cond_wait(&endcond, &dplistmutex);
    }
    pthread_mutex_unlock(&dplistmutex);

    pthread_mutex_lock(&dplistmutex);
    running = 0;
    pthread_cond_broadcast(&startcond);
    pthread_mutex_unlock(&dplistmutex);

    //gettimeofday(&etv, NULL);
    //printf("File Number:%ld Dir Number:%ld Time Usage: %ld second.\n", sfilenum, dirnum, etv.tv_sec - btv.tv_sec);

    /***
    printf("%llu   %llu   %llu   %s\n", 
            dirnum, sfilenum, sfilesize, dir);
    ***/

    for (j = 0; j < threadnum; j++) {
        ret = pthread_join(tid[j], NULL);
        if (ret < 0) {
            fprintf(stdout, "-%d", errno);
            return errno;
        }
    }

    fprintf(stdout, "%llu\t%s\n", sfilesize, dir);

    return 0;
}
