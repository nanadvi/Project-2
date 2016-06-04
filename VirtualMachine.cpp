#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include "Machine.h"
#include "VirtualMachine.h"
#include <vector>
#include <queue>
#include <iostream>
#include <algorithm>

using namespace std;

extern "C"{
    
volatile int tickCounter = 0;
volatile int tickConversion;
volatile bool mainCreated = false;
volatile int mutexIDCounter = 0;
volatile int threadIDCounter = 2;
class Mutex;

class TCB{
public:
    SMachineContext context; 
    TVMThreadID ID; 
    TVMThreadPriority priority;
    TVMThreadState state;
    TVMMemorySize memSize;
    void * stack;
    TVMThreadEntry entry;
    TVMTick tick;
    bool sleeping;
    int result;
    Mutex * mutex;
    void * param;
    TCB();
    TCB(SMachineContext mContext, TVMThreadID id, TVMThreadPriority prio,
    TVMThreadState stat, TVMMemorySize mSize,TVMThreadEntry entry,void * para);
    //~TCB();
};

class Compare{
public:
    bool operator() (TCB *r,TCB *l){
        return (r->priority < l->priority);
    }
};

class Mutex{
public:
    bool locked;
    TVMMutexID ID;
    TVMThreadID owner;
    TCB * ownerTCB;
    priority_queue <TCB*, vector<TCB*>, Compare > waiting;
    Mutex(TVMMutexID id);
};

vector <TCB*> threads;

vector <Mutex*> mutexes;

priority_queue <TCB*, vector<TCB*>, Compare > ready;

TCB * running;

TVMMainEntry VMLoadModule(const char *module);

void idleFun(void *param)
{
    MachineEnableSignals();
    TMachineSignalState sigState;
    MachineResumeSignals(&sigState);
    while(1) 
        MachineResumeSignals(&sigState);
    
}

void mainThreadCreate(){            //creates main and idle threads
    TCB * Main;
    TCB * idle;
    SMachineContext idlecontext;
    idle = new TCB();
    idle->entry = idleFun;
    idle->priority = 0;
    idle->state = VM_THREAD_STATE_READY;
    idle->context = idlecontext;
    idle->memSize = 0x10000;
    idle->stack = malloc(idle->memSize);
    idle->ID = 1;
    MachineContextCreate(&idle->context,idle->entry, idle, 
            NULL,NULL);
    Main = new TCB();
    running = Main; 
    threads.push_back(Main);
    threads.push_back(idle);
    ready.push(idle);
    mainCreated = true;
}

void wrapper(void * param){
    MachineEnableSignals();
    running->entry(param);
    VMThreadTerminate(running->ID);
}

bool queueHelper(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (ready.empty())
        return false;
    if(ready.top()->priority >= running->priority)
        return true;
    else
        return false;
}

bool queueHelper2(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (ready.empty())
        return false;
    if(ready.top()->priority > running->priority)
        return true;
    else
        return false;
}

void scheduler(int code, TCB * thread)
{  
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB * temp = running;
    if (code == 6) //running blocked, run next ready
    {
        running->state = VM_THREAD_STATE_WAITING;
        running = ready.top();
        running->state = VM_THREAD_STATE_RUNNING;
        ready.pop();
        //cout << "Thread ID " << running->ID << " is now running!" << endl;
        MachineContextSwitch(&temp->context,&running->context);
        MachineResumeSignals(&sigState);
    }
    else if (code == 1) //Waiting to ready
    {
        if(queueHelper()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
    else if (code == 7) //waiting to ready for mutex
    {
        if(queueHelper2()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }   
    }
    else if (code == 3 ) //quantum is up, running goes to ready.
    {
        if(queueHelper()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            ready.top()->state = VM_THREAD_STATE_RUNNING;
            running = ready.top();
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
    else if(code == 4){
        thread->state = VM_THREAD_STATE_DEAD;
        running = ready.top();
        ready.pop();
        //cout << "Thread ID " << running->ID << " is now running!" << endl;
        //MachineResumeSignals(&sigState);
        MachineContextSwitch(&temp->context, &running->context); 
        MachineResumeSignals(&sigState);
    }
    else if(code == 5){
        thread->state = VM_THREAD_STATE_READY;
        ready.push(thread);
        if(queueHelper2()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
}

TCB::TCB(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    state = VM_THREAD_STATE_RUNNING;
    MachineContextSave(&context);
    priority = VM_THREAD_PRIORITY_NORMAL;
    //tick = 99999999999999999999999999999999999999999;
    tick = 0;
    sleeping = false;
    result = 0;
    ID = 0;
    MachineResumeSignals(&sigState);
}

TCB::TCB(SMachineContext mContext, TVMThreadID id, TVMThreadPriority prio,
        TVMThreadState stat, TVMMemorySize mSize,TVMThreadEntry mEntry, void * para){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    ID = id;
    priority = prio;
    state = stat;
    memSize = mSize;
    stack = malloc(mSize);
    context = mContext;
    entry = mEntry;
    //tick = 99999999999999999999999999999999999999999;
    tick = 0;
    sleeping = false;
    result = 0;
    param = para;
    MachineResumeSignals(&sigState);
}

Mutex::Mutex(TVMMutexID id)

{
    locked = false;
    ID = id;
}
void sleepCB(void* callbackTCB){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    tickCounter++;
    for (unsigned i = 0; i < threads.size(); i++)
    {
        if (threads[i]->sleeping && tickCounter >= threads[i]->tick)
        {
            threads[i]->sleeping = false;
            //threads[i]->tick = 99999999999999999999;
            threads[i]->tick = 0;
            threads[i]->state = VM_THREAD_STATE_READY;
            ready.push(threads[i]);
        }

    }
    scheduler(3,running);
    MachineResumeSignals(&sigState);
}

TVMStatus VMStart(int tickms, int argc, char *argv[]){
    MachineInitialize();
    MachineRequestAlarm(tickms*1000, sleepCB, NULL);
    tickConversion = tickms;
    TVMMainEntry main = VMLoadModule(argv[0]);
    main(argc, argv);
    TVMMainEntry VMUnLoadModule();
    MachineTerminate();

    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickMS(int *tickmsref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!tickmsref)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    *tickmsref = tickConversion;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickCount(TVMTickRef tickref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!tickref)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    *tickref = tickCounter;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, 
        TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if(!tid || !entry)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    SMachineContext tempcontext;
    TCB * temp;
    *tid = threadIDCounter;
    temp = new TCB(tempcontext,*tid,prio,VM_THREAD_STATE_DEAD,memsize, entry, param);
    threads.push_back(temp);
    threadIDCounter++;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}  

TCB* findThread(TVMThreadID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < threads.size(); i++){
        if(threads[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return threads[i];
        }
    }
    return NULL;
}

TVMStatus VMThreadDelete(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(foundTCB->state != VM_THREAD_STATE_DEAD){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else 
    {
        vector<TCB *>::iterator pos = find(threads.begin(),threads.end(), foundTCB);
        threads.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;  
}

TVMStatus VMThreadActivate(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if(foundTCB->state == VM_THREAD_STATE_DEAD){
        MachineContextCreate(&foundTCB->context,wrapper, foundTCB->param, 
                foundTCB->stack,foundTCB->memSize);
        //foundTCB->state = VM_THREAD_STATE_READY;
        //ready.push(foundTCB);
        scheduler(5,foundTCB);
    }
    else
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadTerminate(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if(foundTCB->state == VM_THREAD_STATE_DEAD){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    scheduler(4, foundTCB);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;  
}

TVMStatus VMThreadID(TVMThreadIDRef threadref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(*threadref);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else
        *threadref = foundTCB->ID;
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!stateref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    *stateref = foundTCB->state;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadSleep(TVMTick tick){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(tick == VM_TIMEOUT_INFINITE)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(tick == VM_TIMEOUT_IMMEDIATE)
    {
        MachineResumeSignals(&sigState);
        scheduler(3,running);
    }
    if (running)
    {
        running->tick = tick+tickCounter;
        running->sleeping = true;
    }
    scheduler(6,running);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mutexref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    Mutex * temp;
    *mutexref = mutexIDCounter;
    temp = new Mutex(*mutexref);
    mutexIDCounter++;
    mutexes.push_back(temp);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

Mutex* findMutex(TVMMutexID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < mutexes.size(); i++){
        if(mutexes[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return mutexes[i];
        }
    }
    return NULL;
}

TVMStatus VMMutexDelete(TVMMutexID mutex){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(foundMutex->locked)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else 
    {
        vector<Mutex *>::iterator pos = find(mutexes.begin(),mutexes.end(), foundMutex);
        mutexes.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(!ownerref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(!foundMutex->locked)
    {
        *ownerref = VM_THREAD_ID_INVALID;
    }
    else
    {
        ownerref = &foundMutex->owner;
    }
    
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
    
}

TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if (!foundMutex->locked)
    {
        running->mutex = foundMutex;
        foundMutex->owner = running->ID;
        foundMutex->ownerTCB = running;
        foundMutex->locked = true;
    }
    else if (timeout == VM_TIMEOUT_INFINITE)
    {
        foundMutex->waiting.push(running);
//        while (foundMutex->ownerTCB != running)
//        {
//            running->tick = tickCounter + 1;
//            running->sleeping = true;
            scheduler(6, running);
        //}
        running->mutex = foundMutex;
        foundMutex->owner = running->ID;
        foundMutex->ownerTCB = running;
        
    }
    else if (timeout == VM_TIMEOUT_IMMEDIATE)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    else
    {
        running->tick = tickCounter + timeout;
        foundMutex->waiting.push(running);
        running->sleeping = true;
        scheduler(6, running);
        if (foundMutex->ownerTCB == running)
        {
            running->mutex = foundMutex;
            foundMutex->owner = running->ID;
            foundMutex->ownerTCB = running;
        }
        else
        {
            MachineResumeSignals(&sigState);
            return VM_STATUS_FAILURE;
        }
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexRelease(TVMMutexID mutex){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if (!foundMutex->locked)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else
    {
        foundMutex->ownerTCB->mutex = NULL;
        if (!foundMutex->waiting.empty())
        {
            foundMutex->ownerTCB = foundMutex->waiting.top();
            foundMutex->owner = foundMutex->ownerTCB->ID;
            foundMutex->waiting.pop();
            foundMutex->ownerTCB->state = VM_THREAD_STATE_READY;
            ready.push(foundMutex->ownerTCB);
            scheduler(7,foundMutex->ownerTCB);
        }
        else
            foundMutex->locked = false;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

void CB(void* calldat, int result)
{   
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB * temp2 = (TCB * ) calldat;
    temp2->result = result;
    temp2->state = VM_THREAD_STATE_READY;
    ready.push(temp2);
    scheduler(1, (TCB*) calldat);
    MachineResumeSignals(&sigState);
}

TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if (!filename || !filedescriptor)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    int result = 0;
    MachineFileOpen(filename,flags, mode, CB, (void*)running);
    scheduler(6, running);
   result = running->result;
   *filedescriptor = result;
   MachineResumeSignals(&sigState);
   if (result < 0)
       return VM_STATUS_FAILURE;
   else 
       return VM_STATUS_SUCCESS;
}

TVMStatus VMFileClose(int filedescriptor){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    int result = 0;
    MachineFileClose(filedescriptor, CB, (void *)running);
    scheduler(6, running);
    result = running->result;
    MachineResumeSignals(&sigState);
    if (result < 0)
       return VM_STATUS_FAILURE;
    else 
       return VM_STATUS_SUCCESS;
    
}

TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if(!data || !length)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    int result = 0;
    MachineFileRead(filedescriptor, data, *length, CB, (void*) running);
    scheduler(6, running);
    result = running->result;
    *length = result;
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;
        
    
}

TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    if (!data || !length)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    int result = 0;
    //cout << "Thread ID " << running->ID << " called VMFileWrite!" << endl;
    MachineFileWrite(filedescriptor, data, *length, CB, (void*) running);
    scheduler(6, running);
    result = running->result;
    *length = result;
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);    
    int result = 0;
    MachineFileSeek(filedescriptor, offset, whence, CB, (void*) running);
    scheduler(6, running);
    result = running->result;
    *newoffset = result;
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;    
}

} // END OF EXTERN "C" 