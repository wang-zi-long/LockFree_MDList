#include <stddef.h>
#include <vector>
#include <math.h>
#include <atomic>
#include <iostream>
#include <unistd.h>
using namespace std;

#define D 2
#define NIL 0x0

//Marking Scripts
#define SetMark(p,m) ((p)|(m))
#define ClearMark(p,m) ((p)&~(uintptr_t)(m))
#define IsMarked(p,m) ((p)&(uintptr_t)(m))
#define F_ADP 0x1U
#define F_PRG 0x2U
#define F_DEL 0x1U
#define F_ALL 0x3U
#define Clear(p) ClearMark(p,F_ALL)

//Data Structures for Lock-Free Priority Queue
struct Node;

struct AdoptDesc{
    Node* curr;
    int dp,dc;
};

struct Node{
    atomic<uintptr_t> val;
    atomic<uintptr_t> mark;
    int key;
    int k[D];
    atomic<uintptr_t> child[D];
    atomic<AdoptDesc*> adesc;
};

#define HeadNode Node

struct Stack{
    Node* node[D];
    HeadNode* head;
};

//Priority Queue Handler
class Handler{
    public:
    Node node;
    AdoptDesc Desc;
    Stack s;
    HeadNode h;
    Node* newNode(){
        return (Node*)malloc(sizeof(Node));
        //return &node;
    }
    AdoptDesc* newDesc(){
        return (AdoptDesc*)malloc(sizeof(AdoptDesc));
        //return &Desc;
    }
    Stack* newStack(){
        return (Stack*)malloc(sizeof(Stack));
        //return &s;
    }
    HeadNode* newHeadNode(){
        return (HeadNode*)malloc(sizeof(HeadNode));
        //return &h;
    }
};

//Priority Queue
class PriorityQueue{
    public:
    int N;
    atomic<uintptr_t> head;
    atomic<Stack*> stack;
    HeadNode firstHeadNode;
    Stack firstStack;

    PriorityQueue(int N):N(N){
        firstHeadNode.val=F_DEL;
        firstHeadNode.mark=F_DEL;
        firstHeadNode.adesc.store(NULL);
        firstHeadNode.key=0;
        setCoords(&firstHeadNode,0);
        for(int i=0;i<D;++i)
            firstHeadNode.child[i].store(NIL);
        head.store((uintptr_t)(&firstHeadNode));
        firstStack.head=&firstHeadNode;
        for(int i=0;i<D;++i)
            firstStack.node[i]=&firstHeadNode;
        stack.store(&firstStack);
    }

    void setCoords(Node* n,int key){
        int basis=ceil(pow(N,1.0/D));
        int quotient=key;
        int *k=n->k;
        for(int i=D-1;i>=0;i--){
            k[i]=quotient%basis;
            quotient=quotient/basis;
        }
    }

    //Inserting a Node into MDList
    bool insert(int key,uintptr_t val,Handler* h){
        Stack *s=h->newStack();
        Node temp_node;
        Node *n=h->newNode();
        n->key=key;
        n->val=val;
        n->mark=val;
        setCoords(n,key);
        for(int i=0;i<D;++i)
            n->child[i].store(NIL);
        while(true){
            Node* pred=NULL;
            int dp=0,dc=0;
            s->head=(HeadNode*)(head.load());
            Node* curr=s->head;
            LocatePlace(h,dp,dc,pred,curr,n,s);
            if(dc==D){
                //this key is already in the queue
                if(curr==NULL||!IsMarked(((Node*)curr)->mark,F_DEL))
                    return false;
                ((Node*)curr)->mark.store(Clear(((Node*)curr)->mark));
                ((Node*)curr)->val.store(val);
                RewindStack(s,curr,pred,dp);
                delete(n);
                return true;
            }
            finishInserting(curr,dp,dc);
            FillNewNode(h,dp,dc,n,curr);
            uintptr_t temp=(uintptr_t)curr;
            if(pred->child[dp].compare_exchange_strong(temp,(uintptr_t)n)){
                finishInserting(n,dp,dc);
                RewindStack(s,n,pred,dp);
                return true;
            }
        }
    }

    inline void LocatePlace(Handler* h,int &dp,int &dc,Node *&pred,Node *&curr,Node *n,Stack *s){
        while(dc<D){
            while(curr!=NULL&&n->k[dc]>curr->k[dc]){
                pred=curr;
                dp=dc;
                finishInserting(curr,dc,dc);
                curr=(Node*)(Clear(curr->child[dc].load()));
            }
            if(curr==NULL||n->k[dc]<curr->k[dc]){
                break;
            }
            s->node[dc]=curr;
            dc++;
        }
    }

    inline void FillNewNode(Handler* h,int dp,int dc,Node *n,Node *curr){
        if(dp<dc){
            AdoptDesc* desc=h->newDesc();
            desc->curr=curr;
            desc->dc=dc;
            desc->dp=dp;
            n->adesc.store(desc);
        }else{
            n->adesc.store(NULL);
        }
        for(int i=0;i<dp;i++) n->child[i]=F_ADP;
        for(int i=dp;i<D;i++) n->child[i]=NIL;
        n->child[dc]=(uintptr_t)curr;
    }

    //Child Adoption
    void finishInserting(Node* n,int dp,int dc){
        if(n==NULL) return;
        AdoptDesc* ad=n->adesc;
        if(ad==NULL||dc<ad->dp||dp>ad->dc) return;
        uintptr_t child;
        Node* curr=ad->curr;
        for(int i=ad->dp;i<ad->dc;++i){
            child=Clear(curr->child[i].fetch_or(F_ADP));
            uintptr_t temp=NIL;
            n->child[i].compare_exchange_strong(temp,child);
        }
        n->adesc.store(NIL);
    }

    //Rewinding deletion stack after insert
    inline void RewindStack(Stack* s,Node* n,Node* pred,int dp){
        //NOTE:no need to rewind stack if node is already deleted ……
        for(bool first_iteration=true;!IsMarked(n->mark,F_DEL);first_iteration=false){
            Stack* sNow=stack.load();
            if(n->key > sNow->node[D-1]->key){
                if(!first_iteration)
                    break;
                *s=*sNow;
            }else{
                for(int i=dp;i<D;i++) 
                    s->node[i]=pred;
            }
            if(stack.compare_exchange_strong(sNow,s))
                break;
        }
    }

    //Deleting Minimal Node
    uintptr_t DeleteMin(Handler* h){
        Stack* sOld=stack.load();
        Stack* s=h->newStack();
        (*s).head=(*sOld).head;
        for(int i=0;i<D;++i)
            (*s).node[i]=(*sOld).node[i];
        printf("delete stack备份完成\n");
        sleep(1);
        printf("开始删除\n");
        int d=D-1;
        while(true){
            Node* last=s->node[d];
            finishInserting(last,d,d);
            Node* child=(Node*)(Clear(last->child[d].load()));
            if(child==NULL){
                if(d==0)return NIL;
                d--;
                continue;
            }
            uintptr_t mark=child->mark;
            if(IsMarked(mark,F_DEL)){
                if(Clear(mark)==NIL){
                    for(int i=d;i<D;i++){
                        s->node[i]=child;
                    }
                }else{
                    s->head=(HeadNode*)(Clear(mark));
                    for(int i=0;i<D;i++)
                        s->node[i]=s->head;
                }
                d=D-1;
            }else{
                if(child->mark.compare_exchange_strong(mark,F_DEL)){
                    for(int i=d;i<D;i++){
                        s->node[i]=child;
                    }
                    stack.compare_exchange_strong(sOld,s);
                    return child->val.load();
                }
            }
        }
        return NIL;
    }

    //Mapping from integer to vector
    vector<int> keyToCoord(int key){
        int basis=ceil(pow(N,1.0/D));
        int quotient=key;
        vector<int> k;
        k.resize(D);
        for(int i=D-1;i>=0;i--){
            k[i]=quotient%basis;
            quotient=quotient/basis;
        }
        return k;
    }

    //Search for a uintptr_t with Coordinates
    Node* searchNode(vector<int> k){
        Node* cur=(Node*)head.load();
        int d=0;
        while(d<D){
            while(cur!=NULL&&k[d]>cur->k[d])
                cur=(Node*)(cur->child[d].load());
            if(cur==NULL||k[d]<cur->k[d])
                return 0;
            d++;
        }
        return cur;
    }
};