#ifndef lockFree_MDList
#define lockFree_MDList

#include <stddef.h>
#include <vector>
#include <math.h>
#include <unordered_map>
#include <atomic>
#include <iostream>
#include <time.h>
#include <thread>
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

#define TYPE_TIMER    1
#define TYPE_SUB      2
#define TYPE_SERVICE  3
#define TYPE_CLIENT   4
#define TYPE_WAITABLE 5

//Data Structures for Lock-Free Priority Queue
struct lock_free_Node;

struct AdoptDesc{
    lock_free_Node* curr;
    int dp,dc;
};

struct lock_free_Node{
    atomic<uintptr_t> val;
    atomic<uintptr_t> mark;
    int key;
    int k[D];
    atomic<uintptr_t> child[D];
    atomic<AdoptDesc*> adesc;
};

#define HeadNode lock_free_Node

struct Stack{
    lock_free_Node* node[D];
    HeadNode* head;
};

//Priority Queue
class PriorityQueue{
public:
    PriorityQueue(int N):N(N){
        firstHeadNode.val=F_DEL;
        firstHeadNode.mark=F_DEL;
        firstHeadNode.adesc.store(NULL);
        firstHeadNode.key=0;
        setCoords(firstHeadNode.k,0);
        for(int i=0;i<D;++i)
            firstHeadNode.child[i].store(NIL);
        head.store((uintptr_t)(&firstHeadNode));
        firstStack.head=&firstHeadNode;
        for(int i=0;i<D;++i)
            firstStack.node[i]=&firstHeadNode;
        stack.store(&firstStack);
    }

    //not first insert
    bool insert(int key,uintptr_t val){
        Stack *s=(Stack*)malloc(sizeof(Stack));
        int k[D];
        setCoords(k,key);
        lock_free_Node *n=(lock_free_Node*)malloc(sizeof(lock_free_Node));
        n->val=val;
        n->mark=val;
        n->key=key;
        for(int i=0;i<D;++i){
            n->child[i].store(NIL);
            n->k[i]=k[i];
        }
        while(true){
            lock_free_Node* pred=NULL;
            int dp=0,dc=0;
            s->head=(HeadNode*)(head.load());
            lock_free_Node* curr=s->head;
            LocatePlace(dp,dc,pred,curr,k,s);
            if(dc==D){
                if(curr==NULL||!IsMarked(((lock_free_Node*)curr)->mark,F_DEL))
                    return false;
                ((lock_free_Node*)curr)->mark.store(Clear(((lock_free_Node*)curr)->mark));
                ((lock_free_Node*)curr)->val.store(val);
                RewindStack(s,curr,pred,dp);
                delete(n);
                return true;
            }
            finishInserting(curr,dp,dc);  
            FillNewNode(dp,dc,n,curr);
            uintptr_t temp=(uintptr_t)curr;
            if(pred->child[dp].compare_exchange_strong(temp,(uintptr_t)n)){
                finishInserting(n,dp,dc);
                RewindStack(s,n,pred,dp);
                return true;
            }
        }
    }

    //Deleting Minimal lock_free_Node
    uintptr_t DeleteMin(){
        Stack* sOld=stack.load();
        Stack* s=(Stack*)malloc(sizeof(Stack));
        *s=*sOld;
        int d=D-1;
        if(sOld!=stack.load()){
            sOld=stack.load();
            *s=*sOld;
        }
        while(true){
            lock_free_Node* last=s->node[d];
            finishInserting(last,d,d);
            lock_free_Node* child=(lock_free_Node*)(Clear(last->child[d].load()));
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
                if(sOld!=stack.load()){
                    sOld=stack.load();
                    *s=*sOld;
                    d=D-1;
                    continue;
                }
                if(child->mark.compare_exchange_strong(mark,F_DEL)){
                    for(int i=d;i<D;i++){
                        s->node[i]=child;
                    }
                    if(!stack.compare_exchange_strong(sOld,s)){
                        child->mark.store(child->val);
                        sOld=stack.load();
                        *s=*sOld;
                        d=D-1;
                        continue;
                    }
                    return child->val.load();
                }
            }
        }
        return NIL;
    }

    //Search for a uintptr_t with Coordinates
    lock_free_Node* searchNode(int key){
        vector<int> k=keyToCoord(key);
        lock_free_Node* cur=(lock_free_Node*)head.load();
        int d=0;
        while(d<D){
            while(cur!=NULL&&k[d]>cur->k[d])
                cur=(lock_free_Node*)(cur->child[d].load());
            if(cur==NULL||k[d]<cur->k[d])
                return 0;
            d++;
        }
        return cur;
    }

private:
    int N;
    atomic<uintptr_t> head;
    atomic<Stack*> stack;
    HeadNode firstHeadNode;
    Stack firstStack;

    void setCoords(int k[],int key){
        int basis=(int)ceil(pow(N,1.0/D));
        int quotient=key;
        for(int i=D-1;i>=0;i--){
            k[i]=quotient%basis;
            quotient=quotient/basis;
        }
    }

    inline void LocatePlace(int &dp,int &dc,lock_free_Node *&pred,lock_free_Node *&curr,int k[],Stack *s){
        while(dc<D){
            while(curr!=NULL&& k[dc] > curr->k[dc]){
                pred=curr;
                dp=dc;
                finishInserting(curr,dc,dc);
                curr=(lock_free_Node*)(Clear(curr->child[dc].load()));
            }
            if(curr==NULL|| k[dc] < curr->k[dc]){
                break;
            }
            s->node[dc]=curr;
            dc++;
        }
    }

    inline void FillNewNode(int dp,int dc,lock_free_Node *n,lock_free_Node *curr){
        if(dp<dc){
            AdoptDesc* desc=(AdoptDesc*)malloc(sizeof(AdoptDesc));
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
    void finishInserting(lock_free_Node* n,int dp,int dc){
        if(n==NULL) return;
        AdoptDesc* ad=n->adesc;
        if(ad==NULL||dc<ad->dp||dp>ad->dc) return;
        uintptr_t child;
        lock_free_Node* curr=ad->curr;
        for(int i=ad->dp;i<ad->dc;++i){
            child=Clear(curr->child[i].fetch_or(F_ADP));
            uintptr_t temp=NIL;
            n->child[i].compare_exchange_strong(temp,child);
        }
        n->adesc.store(NIL);
    }

    //Rewinding deletion stack after insert
    inline void RewindStack(Stack* s,lock_free_Node* n,lock_free_Node* pred,int dp){
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

    //Mapping from integer to vector
    vector<int> keyToCoord(int key){
        int basis=(int)ceil(pow(N,1.0/D));
        int quotient=key;
        vector<int> k;
        k.resize(D);
        for(int i=D-1;i>=0;i--){
            k[i]=quotient%basis;
            quotient=quotient/basis;
        }
        return k;
    }
};

#endif
