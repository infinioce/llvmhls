#ifndef GENPART_H
#define GENPART_H
//===- generatePartition.cpp - generate instruction partitions for DPP -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides passes to generate partitions from dataflow
//
//===----------------------------------------------------------------------===//
#include <sstream>
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "InstructionGraph.h"
#include "generatePartitionsUtil.h"
#include "generateCInstruction.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include <vector>
#include <string>
#include <queue>
#define LONGLATTHRESHOLD 5
using namespace llvm;

    // each dag node is a cycle in the dependency graph
    struct DAGNode4Partition;
    // group of dag node
    struct DAGPartition;
    struct PartitionGen;

    typedef std::map<const Instruction *, DAGNode4Partition *> Insn2DagNodeMap;
    // dag node can be in multiple partitions
    typedef std::map<const DAGNode4Partition*, std::vector<DAGPartition*>*> DagNode2PartitionMap;

    // this is used for srcBB and insBB things
    // the basic block here is a basic block in a partition, which contains a subset
    // of the instructions in the original basic block
    typedef std::map<BasicBlock*, std::vector<Instruction*>*> BBMap2Ins;
    typedef std::map<DAGPartition*, BBMap2Ins*> partitionToBB2InsInfo;
    typedef std::map<DAGPartition*, std::vector<BasicBlock*>*> partitionToBBList;


    // we map instruction to tagName -- a string denoting the name of the channel
    //typedef std::map<Instruction*, std::string> InsMap2Str;




    // this is to keep track of the channels
    // we also map tagName to user Partition -- there can be multiple destination
    typedef std::map<std::string, std::vector<DAGPartition*>*> strMap2PartitionVector;

    typedef BBMap2Ins::iterator BBMapIter;
    static void addBBInsToMap(BBMap2Ins& map, Instruction* curIns)
    {
        BasicBlock* bbKey = curIns->getParent();
        BBMap2Ins::iterator I = map.find(bbKey);
        if(I== map.end())
        {
            std::vector<Instruction*>* curBBIns = new std::vector<Instruction*>();
            curBBIns->push_back(curIns);
            map[bbKey] = curBBIns;
        }
        else
        {
            std::vector<Instruction*>*& curBBIns = map[bbKey];
            // now check if it is already included
            if(std::find(curBBIns->begin(),curBBIns->end(),curIns )== curBBIns->end())
            {
                curBBIns->push_back(curIns);
            }
        }
    }

    /*static std::string generateTerminatorStr(Instruction* ins)
    {
        std::string a = "f";
        // find value
        return a;
    }*/

    static std::string generateSingleStatement(Instruction* curIns, bool remoteSrc, bool remoteDst,
                                               int seqNum,std::vector<std::string>& partitionDecStr,
                                               std::vector<argPair*>& functionArgs, std::vector<argPair*>& fifoArgs, BBMap2outStr* phiPreAssign =0
                                                )
    {
        // first we ll see if the output value is defined already

        // if this is a terminator the the def will be the brTag
        // the Q has name "'brTag'Q", the actual tag has name 'brTag'
        // brTag name is formed by concat the srcBB of branch and the dstBBs
        // it is gonna be a read, then a switch -- with multiple dest

        // sometimes a varDec is not needed, if this is a terminator, and
        // this is actual...note we are pushing the seq number of successors
        // and they are not named

        std::string varDecStr = generateVariableStr(curIns,seqNum);
        if(curIns->isTerminator() && !remoteSrc   )
        {
            // do nothing when this is the terminator and then it is generated locally
        }
        else
        {
            partitionDecStr.push_back(varDecStr);
        }
        //std::string varName = generateVariableName(curIns,seqNum);

        std::string rtStr;
        //special case when it is a remote control flow change
        //read locally
        if(curIns->isTerminator()&&!isa<ReturnInst>(*curIns))
        {
            // as a terminator, we should check if this dude has
            // any successor
            unsigned numSuc = cast<TerminatorInst>(*curIns).getNumSuccessors();
            assert(numSuc < 255);
            if(remoteSrc)
            {
                // apparently we will need a remote src -- we need an argument

                rtStr = generateGettingRemoteBranchTag(cast<TerminatorInst>(*curIns),seqNum, fifoArgs);
                //errs()<<rtStr;

            }
            else
            {
                // we generate the local non return terminator
                // and possibly write the tag into the channel
                rtStr = generateControlFlow(cast<TerminatorInst>(*curIns),remoteDst,seqNum, fifoArgs, functionArgs);

                //errs()<<rtStr;


            }
            // we dont need the the value
            //return generateTerminatorStr(curIns);
        }
        // if this is return, it should be pretty easy.
        else if(isa<ReturnInst>(*curIns))
        {
            // return can never have remote src
            // the ins generating the return value
            // has remote src -- note only srcIns can have remote src, and the data
            // obtained from the channel get used by other
            // instruction in the local partition
            // returnInst doesnt have anything to be used by other instruction
            // it is possible it has functional argument as argument
            rtStr = rtStr+ generateReturn(cast<ReturnInst>(*curIns), functionArgs);
        }
        else if(curIns->isBinaryOp())
        {
            if(remoteSrc)
                rtStr =generateGettingRemoteData(*curIns,seqNum,fifoArgs);
            else
            {
                // this may have stuff from the function arg, also may write to fifo args
                rtStr =generateBinaryOperations(cast<BinaryOperator>(*curIns), remoteDst, seqNum, fifoArgs, functionArgs);
            }
        }
        else if(curIns->getOpcode()<Instruction::MemoryOpsEnd &&curIns->getOpcode() >= Instruction::MemoryOpsBegin  )
        {
            if(remoteSrc)
                rtStr = generateGettingRemoteData(*curIns,seqNum,fifoArgs);
            else
                rtStr = generateMemoryOperations(*curIns,remoteDst, seqNum,fifoArgs, functionArgs);
        }
        else if(curIns->getOpcode()<Instruction::CastOpsEnd && curIns->getOpcode()>= Instruction::CastOpsBegin)
        {
            if(remoteSrc)
                rtStr = generateGettingRemoteData(*curIns,seqNum,fifoArgs);
            else
                rtStr = generateCastOperations(*curIns, remoteDst, seqNum,fifoArgs,functionArgs);
        }
        // other operators --- we only deal with Phi and Select
        // how do we do phi?
        // we need to find all the predecessors of the current basic block
        else if(curIns->getOpcode()==Instruction::PHI)
        {
            if(remoteSrc)
                rtStr = generateGettingRemoteData(*curIns,seqNum,fifoArgs);
            else
            {

                rtStr = generatePhiNode(cast<PHINode>(*curIns), remoteDst, seqNum, phiPreAssign,fifoArgs,functionArgs);
            }

        }
        else if(curIns->getOpcode()==Instruction::Select)
        {
            if(remoteSrc)
                rtStr = generateGettingRemoteData(*curIns,seqNum,fifoArgs);
            else
            {
                rtStr = generateSelectOperations(cast<SelectInst>(*curIns), remoteDst, seqNum,fifoArgs,functionArgs);
            }
        }
        else if(curIns->getOpcode()==Instruction::ICmp || curIns->getOpcode()==Instruction::FCmp)
        {

            // got to generate the cmpare statement
            if(remoteSrc)
                rtStr = generateGettingRemoteData(*curIns,seqNum,fifoArgs);
            else
            {
                rtStr = generateCmpOperations(cast<CmpInst>(*curIns), remoteDst, seqNum,fifoArgs,functionArgs);
            }
        }
        else
        {
            errs()<<" problem unhandled instruction in generate single statement";
            exit(1);
        }

        // if this is
        //errs()<<(*curIns)<<"\n";
        // if it is remote src
        return rtStr;
    }


    static bool searchToBasicBlock(std::vector<BasicBlock*>& storage, BasicBlock* current, BasicBlock* target, BasicBlock* domInter =0 )
    {
        //errs()<<" search to bb starting "<<current->getName()<<" towards "<<target->getName()<<"\n";
        storage.push_back(current);

        if(current == target)
        {

            return true;
        }
        bool keepCurrent = false;
        for(unsigned int ind = 0; ind < current->getTerminator()->getNumSuccessors(); ind++)
        {
            BasicBlock* curSuc = current->getTerminator()->getSuccessor(ind);
            if(std::find(storage.begin(),storage.end(),curSuc) != storage.end())
            {
                //curSuc already in the array, try the next one
                continue;
            }
            // if this path goes through dominator then its disregarded
            if(curSuc == domInter)
                continue;
            bool found = searchToBasicBlock(storage, curSuc, target);
            if(found)
            {
                //storage.push_back(curSuc);
                keepCurrent = true;
            }
        }
        if(!keepCurrent)
            storage.pop_back();
        return keepCurrent;
    }
    static void mergeInto(std::vector<BasicBlock*>& small, std::vector<BasicBlock*>& big)
    {
        //errs()<<"into merge";
        for(unsigned int pathInd=0; pathInd < small.size(); pathInd++)
        {
            BasicBlock* curPathBB = small.at(pathInd);
            if(std::find(big.begin(),big.end(),curPathBB)==big.end())
            {
                big.push_back(curPathBB);
            }
        }
        //errs()<<" outo merge";
    }


    static void addPathBBsToBBMap(BBMap2Ins& dstBBs, BasicBlock* dominator, std::vector<BasicBlock*>& AllBBs )
    {
        std::vector<BasicBlock*> curPathStorage;

        for(BBMapIter bmi = dstBBs.begin(), bme = dstBBs.end(); bmi!=bme; ++bmi)
        {


            BasicBlock* dest=bmi->first;
           // errs()<<"serach towards " << dest->getName()<<"\n";
            if(searchToBasicBlock(curPathStorage,dominator,dest))
            {
                // got to put everything on the the AllBBs
                mergeInto(curPathStorage,AllBBs);

            }
            curPathStorage.clear();

        }
    }
    static void searchToGroupAndAdd(BasicBlock* curBB, BasicBlock* dominator, BBMap2Ins& destBBs,
                                    std::vector<BasicBlock*>& curPathStorage,std::vector<BasicBlock*>& AllBBs)
    {
        for(BBMapIter bmi2 = destBBs.begin(), bme2 = destBBs.end(); bmi2!=bme2; ++bmi2)
        {
            BasicBlock* destBB = bmi2->first;
            if(searchToBasicBlock(curPathStorage,curBB,destBB,dominator))
            {
                // now put everything into the AllBBs vector
                mergeInto(curPathStorage,AllBBs);
            }
            curPathStorage.clear();

        }
    }

    struct DAGNode4Partition{
        std::vector<InstructionGraphNode*>* dagNodeContent;
        bool singleIns;
        int sccLat;
        bool hasMemory;
        bool covered;
        int seqNum;
        void init()
        {
            singleIns = false;
            sccLat = 0;
            hasMemory = false;
            covered = false;
            seqNum = -1;
        }
        void print()
        {
            for(unsigned int insI = 0; insI < dagNodeContent->size(); insI++)
            {
                Instruction* curIns = dagNodeContent->at(insI)->getInstruction();
                errs()<<"\t"<<*curIns<<"\n";
            }
        }

    };
    struct PartitionGen : public FunctionPass {
      static char ID;  // Pass identification, replacement for typeid
      raw_ostream &Out;
      raw_ostream &fifoDes;
      Function* curFunc;
      partitionToBB2InsInfo srcBBsInPartition;
      partitionToBB2InsInfo insBBsInPartition;
      // each partition's bb list
      partitionToBBList allBBsInPartition;

      static int partGenId;
      //InsMap2Str ins2Channel;
      strMap2PartitionVector channelFanOutPartition;


      //std::vector<DAGNode4Partition*> allDAGNodes;


      std::vector<DAGPartition*> partitions;

      // from instruction to node
      Insn2DagNodeMap dagNodeMap;
      // from node to partition
      DagNode2PartitionMap dagPartitionMap;
      //

      //PartitionGen() : FunctionPass(ID){}
      PartitionGen(raw_ostream &out, raw_ostream &out2) : FunctionPass(ID),Out(out),fifoDes(out2) {}
      bool runOnFunction(Function& func);
      /*void initializeOut(raw_ostream &out)
      {
          Out(out);
      }*/

      void generatePartition(std::vector<DAGNode4Partition*> *dag);

      void generateControlFlowPerPartition();
      void print(raw_ostream &O, const Module* = 0) const { }

      void DFSCluster(DAGNode4Partition* curNode, DAGPartition* curPartition);
      void BFSCluster(DAGNode4Partition* curNode);
      void BarrierCluster(std::vector<DAGNode4Partition*> *dag);
      bool DFSFindPartitionCycle(DAGPartition* nextHop);

      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<InstructionGraph>();
        AU.addRequiredTransitive<DominatorTree>();
        AU.addRequired<PostDominatorTree>();
        AU.addRequired<LoopInfo>();
        AU.setPreservesAll();
      }
      std::vector<DAGPartition*>* getPartitionFromIns(Instruction* ins)
      {
          DAGNode4Partition* node = dagNodeMap[const_cast<Instruction*>(ins)];
          std::vector<DAGPartition*>* part = dagPartitionMap[const_cast<DAGNode4Partition*>(node)];
          return part;
      }
      void generateCFromBBList(DAGPartition* pa, LoopInfo* li, DominatorTree* dt);
      void addChannelAndDepPartition(bool &thereIsPartitionReceiving, Instruction* insPt,
                                                   std::string& channelStr, DAGPartition* destPart, int channelType, int seqNum);


    };
    struct DAGPartition{
        // a partition contains a set of dagNode
        std::vector<DAGNode4Partition*> partitionContent;

        // let's add the pointer to the maps

        bool containMemory;
        bool containLongLatCyc;

        bool cycleDetectCovered;
        //DagNodeMapTy* insToDagNode;
        //DagPartitionMapTy* dagNodeToPartition;
        PartitionGen* top;

        void init(PartitionGen* tt )
        {
            containMemory = false;
            containLongLatCyc = false;
            cycleDetectCovered = false;
            top = tt;
        }
        void print()
        {
            for(unsigned int nodeI = 0; nodeI < partitionContent.size(); nodeI++)
            {
                DAGNode4Partition* curNode = partitionContent.at(nodeI);
                curNode->print();
                errs()<<"\n";
            }
        }
        void addDagNode(DAGNode4Partition* dagNode,DagNode2PartitionMap &nodeToPartitionMap )
        {
            dagNode->print();
            partitionContent.push_back(dagNode);
            dagNode->covered=true;
            containMemory |= dagNode->hasMemory;
            containLongLatCyc |= (dagNode->sccLat >= LONGLATTHRESHOLD);
            // each dagNode maps to at least one partition
            // we can make vector here

            std::vector<DAGPartition*>* listOfDp;
            //= nodeToPartitionMap[dagNode];
            if(nodeToPartitionMap.find(dagNode) == nodeToPartitionMap.end())
            {
                listOfDp = new std::vector<DAGPartition*>();
                nodeToPartitionMap[dagNode] = listOfDp;
            }
            else
                listOfDp = nodeToPartitionMap[dagNode];
            listOfDp->push_back(this);
            //nodeToPartitionMap[dagNode] = listOfDp;
        }



        void generateBBList(DominatorTree* DT, PostDominatorTree* PDT)
        {
            BBMap2Ins* sourceBBs = new BBMap2Ins;
            BBMap2Ins* insBBs = new BBMap2Ins;

            std::vector<Instruction*> srcInstructions;
            for(unsigned int nodeInd = 0; nodeInd < partitionContent.size(); nodeInd++)
            {
                DAGNode4Partition* curNode = partitionContent.at(nodeInd);
                for(unsigned int insInd = 0; insInd< curNode->dagNodeContent->size(); insInd++)
                {
                    Instruction* curIns = curNode->dagNodeContent->at(insInd)->getInstruction();

                    addBBInsToMap(*insBBs,curIns);
                    unsigned int numOp = curIns->getNumOperands();
                    for(unsigned int opInd = 0; opInd < numOp; opInd++)
                    {
                        Value* curOp = curIns->getOperand(opInd);
                        if(isa<Instruction>(*curOp))
                        {
                            Instruction* srcIns = &(cast<Instruction>(*curOp));
                            srcInstructions.push_back(srcIns);
                            addBBInsToMap(*sourceBBs,srcIns);
                        }
                    }
                }
            }



            // now make the instructions



            // dominator for src and ins BBs
            // this will be the first basicblock we need to convert
            // then we shall see where each bb diverge?
            BasicBlock* dominator=0;
            BasicBlock* prevBB=0;
            BasicBlock* postDominator=0;
            BasicBlock* prevBBPost = 0;

            for(BBMapIter bmi = sourceBBs->begin(), bme = sourceBBs->end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                //errs()<<curBB->getName()<<"\n";
                // now print out the source

                if(prevBB!=0)
                {
                    dominator = DT->findNearestCommonDominator(prevBB, curBB);
                    //errs()<<"\n"<<dominator->getName()<<"is the com anc of "<<prevBB->getName()<<" and "<<curBB->getName()<<"\n";
                    prevBB = dominator;
                }
                else
                    prevBB = curBB;

                if(prevBBPost!=0)
                {
                    postDominator = PDT->findNearestCommonDominator(prevBBPost,curBB);
                    //errs()<<"\n"<<postDominator->getName()<<"is the post com dom of "<<prevBBPost->getName()<<" and "<<curBB->getName()<<"\n";
                    prevBBPost = postDominator;
                }
                else
                    prevBBPost = curBB;
            }


            for(BBMapIter bmi = insBBs->begin(), bme = insBBs->end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                //errs()<<curBB->getName()<<"\n";
                // now print out the source

                if(prevBB!=0)
                {
                    dominator = DT->findNearestCommonDominator(prevBB, curBB);
                    //errs()<<"\n"<<dominator->getName()<<"is the com anc of "<<prevBB->getName()<<" and "<<curBB->getName()<<"\n";
                    prevBB = dominator;
                }
                else
                    prevBB = curBB;

                if(prevBBPost!=0)
                {
                    postDominator = PDT->findNearestCommonDominator(prevBBPost,curBB);
                   // errs()<<"\n"<<postDominator->getName()<<"is the post com dom of "<<prevBBPost->getName()<<" and "<<curBB->getName()<<"\n";
                    prevBBPost = postDominator;
                }
                else
                    prevBBPost = curBB;
            }
            // let's filter out those irrelevant
            // how do we do this,
            // start from each srcBBs, search backward until dominator,
            // everything in between is to be needed, -- if they are not srcBB nor insBBs
            // their terminator output would be needed

            // then go from each srcBBs and insBBs forward, until post dominator,
            // everything in between is needed, is it?
            // in the case of two BBs, if one BB is never reaching another BB
            // without going through dominator, then when this BB exits, we can
            // just wait in the dominator, so the procedure should be use
            // each BB as starting point to search for all other BBs, if any BB
            // is found all path to that BB should be included? if none can be found
            // then getting out of this BB is same as getting out of our subgraph

            // naturally if we have everything within one BB, we just need that BB
            std::vector<BasicBlock*>* AllBBs = new std::vector<BasicBlock*>();
            // some of these BBs wont be in the map, in which case, we just need the terminator
            AllBBs->push_back(dominator);

            addPathBBsToBBMap(*sourceBBs,dominator,*AllBBs);
            addPathBBsToBBMap(*insBBs,dominator,*AllBBs);
            // now  we do the n^2 check to see if anybody goes to anybody else?
            for(BBMapIter bmi = sourceBBs->begin(), bme = sourceBBs->end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                std::vector<BasicBlock*> curPathStorage;

                searchToGroupAndAdd(curBB,dominator,*sourceBBs,curPathStorage,*AllBBs);
                // same thing for insBB
                searchToGroupAndAdd(curBB,dominator,*insBBs,curPathStorage,*AllBBs);

            }
            for(BBMapIter bmi = insBBs->begin(), bme = insBBs->end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                std::vector<BasicBlock*> curPathStorage;

                searchToGroupAndAdd(curBB,dominator,*sourceBBs,curPathStorage,*AllBBs);
                // same thing for insBB
                searchToGroupAndAdd(curBB,dominator,*insBBs,curPathStorage,*AllBBs);

            }

            // now all the basicblocks are added into the allBBs
            (top->srcBBsInPartition)[this]=sourceBBs;
            (top->insBBsInPartition)[this]=insBBs;
            (top->allBBsInPartition)[this]=AllBBs;

// also what is the instructions after the partitioning
// let's check what's in sourceBBs,
// what's in insBBs,
            /*
errs()<<" print out partition content \n";
this->print();

errs()<<"=========================================================================\n";
errs()<<" print out the insBBs\n";
for(BBMap2Ins::iterator insIt = insBBs->begin(), insEnd = insBBs->end(); insIt!=insEnd; insIt++)
{
    BasicBlock* curBB = insIt->first;
    errs()<<curBB->getName()<<":\n";
    std::vector<Instruction*>* curBBinsBB = insIt->second;
    for(unsigned insInd = 0; insInd < curBBinsBB->size(); insInd++)
    {
        Instruction* nowIns = curBBinsBB->at(insInd);
        errs()<<"\t"<<*nowIns<<"\n";
    }

}

errs()<<" print out the srcBBs\n";
for(BBMap2Ins::iterator insIt = sourceBBs->begin(), insEnd = sourceBBs->end(); insIt!=insEnd; insIt++)
{
    BasicBlock* curBB = insIt->first;
    errs()<<curBB->getName()<<":\n";
    std::vector<Instruction*>* curBBinsBB = insIt->second;
    for(unsigned insInd = 0; insInd < curBBinsBB->size(); insInd++)
    {
        Instruction* nowIns = curBBinsBB->at(insInd);
        errs()<<"\t"<<*nowIns<<"\n";
    }

}
errs()<<"=========================================================================\n";
*/
            errs()<<"dominator is "<<dominator->getName()<<" post dom is "<<postDominator->getName()<<"\n";

            // go to release memory here

        }




    };
    static bool needANewPartition(DAGPartition* curPartition, DAGNode4Partition* curNode)
    {
        if((curNode->hasMemory  &&  (curPartition->containLongLatCyc || curPartition->containMemory))||
           ((!curNode->singleIns  && curNode->sccLat >= LONGLATTHRESHOLD)&& (curPartition->containMemory)))
            return true;
        else
            return false;
    }
    static void findDependentNodes(DAGNode4Partition* curNode, Insn2DagNodeMap &nodeLookup, std::vector<DAGNode4Partition*> &depNodes)
    {
        // how do we do this?
        // iterate through the instructionGraphNodes in curNode, and then
        // for each of them, look at instruction, and then lookup the node
        // in the map
        for(unsigned int i=0; i< curNode->dagNodeContent->size(); i++)
        {
            InstructionGraphNode* curInsNode = curNode->dagNodeContent->at(i);

            for(InstructionGraphNode::iterator depIns = curInsNode->begin(), depInsE = curInsNode->end();
                depIns != depInsE; ++depIns)
            {
                Instruction* curDepIns = depIns->second->getInstruction();
                DAGNode4Partition* node2add = nodeLookup[curDepIns];
                depNodes.push_back(node2add);

            }
        }
    }





char PartitionGen::ID = 0;
int PartitionGen::partGenId = 0;
//static RegisterPass<PartitionGen>
//Y("dpp-gen", "generate decoupled processing functions for each function CFG");


bool compareDagNode(DAGNode4Partition* first, DAGNode4Partition* second)
{
    return first->seqNum < second->seqNum;
}



void PartitionGen::BarrierCluster(std::vector<DAGNode4Partition*> *dag)
{
    DAGPartition* curPartition = new DAGPartition;
    curPartition->init(this);
    partitions.push_back(curPartition);

    for(unsigned int dagInd = 0; dagInd < dag->size(); dagInd++)
    {

        DAGNode4Partition* curDagNode = dag->at(dagInd);
        if(needANewPartition(curPartition,curDagNode))
        {
            curPartition = new DAGPartition;
            curPartition->init(this);
            partitions.push_back(curPartition);
        }
        curPartition->addDagNode(curDagNode,dagPartitionMap);
    }
}

void PartitionGen::BFSCluster(DAGNode4Partition* curNode)
{
    //if(needANewPartition(curPartition,curNode))
    //    return;
    //else

    // create a new partition and add the seed node
    DAGPartition* curPartition = new DAGPartition;
    curPartition->init(this);
    partitions.push_back(curPartition);

    std::queue<DAGNode4Partition*> storage;

    curPartition->addDagNode(curNode,  dagPartitionMap);
    storage.push(curNode);
    //curPartition->addDagNode(curNode,  dagPartitionMap);
    while(storage.size()!=0)
    {
        DAGNode4Partition* nodeCheck = storage.front();
        storage.pop();
        std::vector<DAGNode4Partition*> depNodes;
        findDependentNodes(nodeCheck, dagNodeMap,depNodes);
        std::sort(depNodes.begin(),depNodes.end(), compareDagNode);
        for(unsigned int j = 0; j<depNodes.size(); j++)
        {

            DAGNode4Partition* nextNode = depNodes.at(j);
            if(nextNode->covered==false)
            {
                if(needANewPartition(curPartition,nextNode))
                {
                    // we need to create a new partition
                    curPartition = new DAGPartition;
                    curPartition->init(this);
                    partitions.push_back(curPartition);
                }
                curPartition->addDagNode(nextNode,  dagPartitionMap);
                storage.push(nextNode);
            }
        }
    }

}

void PartitionGen::DFSCluster(DAGNode4Partition* curNode, DAGPartition* curPartition)
{

    if(needANewPartition(curPartition,curNode))
        return;
    else
        curPartition->addDagNode(curNode,  dagPartitionMap);

    // now we shall figure out the next hop
    std::vector<DAGNode4Partition*> depNodes;
    findDependentNodes(curNode, dagNodeMap,depNodes);
    std::sort(depNodes.begin(),depNodes.end(), compareDagNode);
    // should sort the vector according to the seqNum of the nodes
    for(unsigned int j = 0; j<depNodes.size(); j++)
    {
        DAGNode4Partition* nextNode = depNodes.at(j);
        if(nextNode->covered==false)
            DFSCluster(nextNode, curPartition);
    }
}


#define NEWPARTEVERYSEED
//#define USEBFSCLUSTER
//#define USEBFSCLUSTER
#define USEBARRIERCLUSTER
void PartitionGen::generatePartition(std::vector<DAGNode4Partition*> *dag)
{

#ifdef USEBARRIERCLUSTER
    BarrierCluster(dag);
#else

    for(unsigned int dagInd = 0; dagInd < dag->size(); dagInd++)
    {
        DAGNode4Partition* curNode = dag->at(dagInd);
        if(curNode->covered)
            continue;
#ifdef USEDFSCLUSTER
        DAGPartition* curPartition = 0;

#ifdef NEWPARTEVERYSEED
        curPartition = new DAGPartition;
        curPartition->init();
        partitions.push_back(curPartition);

#else
        if(!curPartition)
        {
            curPartition = new DAGPartition;
            curPartition->init();
            partitions.push_back(curPartition);
        }
        // if curnode has memory or it is long latency cycle
        else if(needANewPartition(curPartition,curNode))
        {
            // time to start a new partition
            curPartition = new DAGPartition;
            curPartition->init();
            partitions.push_back(curPartition);
        }
#endif
        // DFS has problem
        DFSCluster(curNode, curPartition);
#else
        BFSCluster(curNode);

#endif
    }
#endif


    /*errs()<<"#of partitions :"<<partitions.size()<<"\n";
    for(unsigned int ai = 0; ai<partitions.size(); ai++)
    {
        errs()<<"partitions :"<<ai <<partitions.at(ai)->partitionContent.size()<<"\n";
    }
    for(unsigned int ai = 0; ai<partitions.size(); ai++)
    {
        DAGPartition* curPar = partitions.at(ai);
        std::vector<DAGNode4Partition*>* nodeVector = &(curPar->partitionContent);
        errs()<<"partition"<< ai<<"\n";
        for(unsigned int nodeI = 0; nodeI <nodeVector->size(); nodeI++)
        {

            DAGNode4Partition* curNode = nodeVector->at(nodeI);
            for(unsigned int insI = 0; insI < curNode->dagNodeContent->size(); insI++)
            {
                Instruction* curIns = curNode->dagNodeContent->at(insI)->getInstruction();
                errs()<<"\t"<<*curIns<<"\n";
            }
            errs()<<"\n";
        }
    }*/
}



void PartitionGen::generateControlFlowPerPartition()
{
    // go through every partition, for each partition
    // go through all the instructions
    // for each instruction, find where their operands are from
    // then these are all the basic blocks we need ? dont we need more
    // control dependence communication -- lets not have it and check
    // for cyclic dependence

    // lets check if there is any cycle between the partitions

    for(unsigned int pi = 0; pi < partitions.size(); pi++)
    {
        errs()<<pi<<" partition geting\n";
        DAGPartition* curPart = partitions.at(pi);

        if(DFSFindPartitionCycle(curPart))
        {
            errs()<<" cycle discovered quit quit\n";
            // now see which partitions are in the cycle
            for(unsigned int pie = 0; pie < partitions.size(); pie++)
            {
                    DAGPartition* curParte = partitions.at(pie);
                    if(curParte->cycleDetectCovered)
                    {
                        errs()<<"cycle contains "<<"\n";
                        curParte->print();
                        errs()<< "\n";
                    }
            }

            exit(1);
        }

    }
    errs()<<" no cycle discovered\n";
    // starting the actual generation of function
    DominatorTree* DT  = getAnalysisIfAvailable<DominatorTree>();
    PostDominatorTree* PDT = getAnalysisIfAvailable<PostDominatorTree>();

    for(unsigned int partInd = 0; partInd < partitions.size(); partInd++)
    {
        DAGPartition* curPart = partitions.at(partInd);
        curPart->generateBBList(DT, PDT);

        errs()<<"done part\n";
        errs()<<"[\n";
        std::vector<BasicBlock*>* AllBBs
                = allBBsInPartition[curPart];
        for(unsigned int allBBInd = 0; allBBInd < AllBBs->size(); allBBInd++)
        {
            errs()<<AllBBs->at(allBBInd)->getName();
            errs()<<"\n";
        }
        errs()<<"]\n";

    }
    // all BB list generated, lets check the partitions
    // now one by one we will generate the C code for each partition
    for(unsigned int partInd = 0; partInd < partitions.size(); partInd++)
    {
        DAGPartition* curPart = partitions.at(partInd);
        LoopInfo* LI = getAnalysisIfAvailable<LoopInfo>();
        generateCFromBBList(curPart,LI,DT);
    }


}
bool PartitionGen::DFSFindPartitionCycle(DAGPartition* dp)
{

    if(dp->cycleDetectCovered)
        return true;


    dp->cycleDetectCovered = true;
    std::vector<DAGNode4Partition*>* curPartitionContent = &(dp->partitionContent);
    for(unsigned int ni = 0; ni < curPartitionContent->size();ni++)
    {
        DAGNode4Partition* curNode = curPartitionContent->at(ni);
        // for this curNode, we need to know its dependent nodes
        // then each of the dependent node will generate the next hop
        std::vector<DAGNode4Partition*> depNodes;
        findDependentNodes(curNode,dagNodeMap,depNodes);
        for(unsigned int di =0 ; di<depNodes.size(); di++)
        {
            DAGNode4Partition* nextNode=depNodes.at(di);

            std::vector<DAGPartition*>* nextHopPartitions = dagPartitionMap[nextNode];
            DAGPartition* nextHop = nextHopPartitions->front();
            if(nextHop==dp)
                continue;
            if(DFSFindPartitionCycle(nextHop))
            {
                return true;
            }
        }

    }
    dp->cycleDetectCovered = false;
    return false;
}
void PartitionGen::addChannelAndDepPartition(bool &thereIsPartitionReceiving, Instruction* insPt,
                                             std::string& channelStr, DAGPartition* destPart,int channelType, int insSeqNum)
{
    if(!thereIsPartitionReceiving)
    {
        channelStr= generateChannelString(channelType,insSeqNum,insPt->getParent()->getName());
        //ins2Channel[insPt]= channelStr;

    }
    std::vector<DAGPartition*>*& channelToDestPartitions = channelFanOutPartition[channelStr];

    if(thereIsPartitionReceiving)
        errs()<<channelToDestPartitions->size()<<"\n";

    if(!thereIsPartitionReceiving)
        channelToDestPartitions = new std::vector<DAGPartition*>();

    channelToDestPartitions->push_back(destPart);
    thereIsPartitionReceiving=true;
}

void PartitionGen::generateCFromBBList(DAGPartition* pa, LoopInfo* li, DominatorTree* dt)
{
    //BBMap2Ins& srcBBs, BBMap2Ins& insBBs, std::vector<BasicBlock*>& BBList
    BBMap2Ins* srcBBs = srcBBsInPartition[pa];
    BBMap2Ins* insBBs = insBBsInPartition[pa];
    std::vector<BasicBlock*>* BBList = allBBsInPartition[pa];
    // now find the dominator for this list of BB
    BasicBlock* domBB = BBList->at(0);
    for(unsigned int bbInd = 1; bbInd < BBList->size(); bbInd++)
    {
        domBB = dt->findNearestCommonDominator(domBB,BBList->at(bbInd));
    }
    // the dominator must be inside the list
    assert(std::find(BBList->begin(),BBList->end(),domBB)!=BBList->end());
    // we can generate the definition string, and create a mapping of instruction to def string
    //InsMap2Str ins2Def; // wont be necessary



    //BBMap2outStr bb2Str;
    // each vector of string's last string is the terminator,
    // when we finally output the strings, we need to check if there is any phi
    // node inserted assignment before the terminator
    std::vector<std::vector<std::string>*> curPartitionBBStr;
    // all declarations of the var used in the partition
    std::vector<std::string> partitionDecStr;

    BBMap2outStr phiPreAssign;

    //FIXME
    // we need to figure out the argument, these includes
    // the argument from the function -- these can be given to every partition
    // they may not always use it;
    // then we have those FIFOs --- in and out FIFOs
    std::vector<argPair*> functionArgs;
    std::vector<argPair*> fifoArgs;

    // this is also the place where we generate the connection
    // description?
    for(unsigned int curBBInd = 0; curBBInd < BBList->size(); curBBInd++)
    {
        BasicBlock* curBB = BBList->at(curBBInd);
        std::vector<std::string>* curBBStrArray = new std::vector<std::string>();
        //bb2Str[curBB] = curBBStrArray;
        curPartitionBBStr.push_back(curBBStrArray);
        std::string bbname =  curBB->getName();
        bbname.append(":\n");
        curBBStrArray->push_back(bbname);

        if(srcBBs->find(curBB)==srcBBs->end() && insBBs->find(curBB)==insBBs->end())
        {
            // basicblock takes in a branch tag only -- definitely not sending stuff to remote
            Instruction* curTerm = curBB->getTerminator();
            // shouldnt be return coz the return block isnt in a path
            assert(!isa<ReturnInst>(*curTerm));
            int seqNum = curTerm->getParent()->getInstList().size()-1;
            std::string termStr = generateSingleStatement(curTerm,true,false,seqNum,partitionDecStr,functionArgs, fifoArgs);
            curBBStrArray->push_back(termStr);
            continue;
        }
        std::vector<Instruction*>* srcIns = 0;
        std::vector<Instruction*>* actualIns = 0;
        if(srcBBs->find(curBB)!=srcBBs->end())
            srcIns = (*srcBBs)[curBB];

        if(insBBs->find(curBB)!=insBBs->end())
            actualIns = (*insBBs)[curBB];

        // now this bb is either a srcBB or actualInsBB
        int instructionSeq = -1;
        for(BasicBlock::iterator insPt = curBB->begin(), insEnd = curBB->end(); insPt != insEnd; insPt++)
        {
            errs()<<instructionSeq <<cast<Instruction>(*insPt)<<"\n";
            instructionSeq ++;
            // it is possible that this instruction is not in srcBB nor insBB
            // then this is not converted to c, but if this is the terminator
            // we need t read the branch tag unless its return
            if(srcIns!=0 && (std::find(srcIns->begin(),srcIns->end(), insPt)==srcIns->end()) &&
               actualIns!=0 && (std::find(actualIns->begin(),actualIns->end(),insPt)==actualIns->end())
             )
            {
                errs()<<"nothing "<<cast<Instruction>(*insPt)<<"\n";

                if(insPt->isTerminator() && !isa<ReturnInst>(*insPt) )
                {
                    std::string srcInsStr = generateSingleStatement(insPt,true,false,instructionSeq,partitionDecStr, functionArgs,fifoArgs);
                    curBBStrArray->push_back(srcInsStr);
                }
            }

            // another case is one of srcIns & actualIns is zero, but the non zero one
            // does not contain this instruction....we still need to add the control flow
            // for this basic block
            if(srcIns!=0 && actualIns==0)
            {
                if(std::find(srcIns->begin(),srcIns->end(), insPt)==srcIns->end())
                {
                    if(insPt->isTerminator() && !isa<ReturnInst>(*insPt) )
                        curBBStrArray->push_back(generateSingleStatement(insPt,true,false,instructionSeq,partitionDecStr,functionArgs,fifoArgs));
                }
            }
            if(srcIns==0 && actualIns!=0)
            {
                if(std::find(actualIns->begin(),actualIns->end(),insPt)==actualIns->end())
                {
                    if(insPt->isTerminator() && !isa<ReturnInst>(*insPt) )
                        curBBStrArray->push_back(generateSingleStatement(insPt,true,false,instructionSeq,partitionDecStr,functionArgs,fifoArgs));
                }
            }

            if(srcIns!=0 && !(std::find(srcIns->begin(),srcIns->end(), insPt)==srcIns->end()))
            {
                //
                // this instruction is in the srcBB, meaning its output is used by this partition
                // meaning the we should insert a blocking read from FIFO -- unless its in the actual
                // ins also
                // its not in the actual ins
                if(actualIns==0 || (actualIns!=0 && (std::find(actualIns->begin(),actualIns->end(),insPt)==actualIns->end())))
                {
                    errs()<<"src   "<<cast<Instruction>(*insPt)<<"\n";
                    std::string srcInsStr = generateSingleStatement(insPt,true,false,instructionSeq,partitionDecStr,functionArgs,fifoArgs);
                    curBBStrArray->push_back(srcInsStr);
                }
            }
            if(actualIns!=0 && !(std::find(actualIns->begin(),actualIns->end(),insPt)==actualIns->end()))
            {
                errs()<<"actual   "<<cast<Instruction>(*insPt)<<"\n";

                // this instruction is the actual, let's see if this ins is used by
                // instruction in other partition --
                std::vector<DAGPartition*>* curInsOwners = getPartitionFromIns(insPt);
                DAGPartition* curInsPart = curInsOwners->at(0);
                //FIXME:seems redundant, should be deleted I think
                assert(curInsPart == pa);
                // now check all its user to see if it is being used by others
                // if it is then we should also add an entry in the channel list
                // also if this is an terminator, it may not have any use, but we may
                // still send brTag

                bool thereIsPartitionReceiving = false;
                std::string channelStr;
                if(insPt->isTerminator()&& !isa<ReturnInst>(*insPt))
                {

                    // are there other partitions having the same basicblock
                    for(unsigned sid = 0; sid < partitions.size(); sid++)
                    {
                        DAGPartition* destPart = partitions.at(sid);
                        if(curInsPart == destPart)
                            continue;
                        std::vector<BasicBlock*>* destPartBBs = allBBsInPartition[destPart];

                        if(std::find(destPartBBs->begin(),destPartBBs->end(),curBB)!=destPartBBs->end())
                        {
                            // add the branchtage channel
                            addChannelAndDepPartition(thereIsPartitionReceiving,insPt,channelStr,destPart,0,instructionSeq);

                        }
                    }
                }
                else
                {
                     errs()<<"begin add channel";

                    for(Value::use_iterator curUser = insPt->use_begin(), endUser = insPt->use_end(); curUser != endUser; ++curUser )
                    {
                        // now we look at each use, these instruction belows to some DAGNode which belongs to some
                        // DAGPartition
                        assert(isa<Instruction>(*curUser));

                        std::vector<DAGPartition*>* curUseOwners=getPartitionFromIns(cast<Instruction>(*curUser));
                        DAGPartition* curUsePart = curUseOwners->at(0);

                        if(curUsePart == curInsPart)
                            continue;
                         // add the data channel to the database
                        errs()<<"before adding channel\n";
                         addChannelAndDepPartition(thereIsPartitionReceiving,insPt,channelStr,curUsePart,1,instructionSeq);
                         errs()<<"after adding channel\n";
                    }
                }
                errs()<<"done add channel";
                std::string actualInsStr = generateSingleStatement(insPt,false,thereIsPartitionReceiving,instructionSeq,partitionDecStr,functionArgs,fifoArgs,&phiPreAssign);
                curBBStrArray->push_back(actualInsStr);
                errs()<<"done actual\n";
            }



        }
    }

    errs()<<"before endgroup\n";
    std::string endgroup = generateEndBlock(BBList);
    // the only guy who do not need to reside on the while 1 loop is the dude with the
    // terminator of the entry block
    errs()<<"after endgroup\n";
    // now we can output


//generate function name
    this->Out<<curFunc->getName()<<int2Str(partGenId);
    this->Out<<"(";
    /*****this is the part where we make the argument*****/
    for(unsigned k=0; k<functionArgs.size();k++)
    {
        argPair* curP = functionArgs.at(k);
        // make sure there is no repeat
        bool added = false;
        for(unsigned ki=0; ki<k; ki++)
        {
            // check if we already seen it
            argPair* checkP = functionArgs.at(ki);
            if(checkP->argName.compare( curP->argName)==0)
            {
                added = true;
                break;
            }
        }
        if(!added)
        {
            std::string argDec = generateArgStr(curP);
            if(k!=0)
                this->Out<<",\n";
            this->Out<<argDec;
        }
    }

    for(unsigned k=0; k<fifoArgs.size(); k++)
    {
        argPair* curP = fifoArgs.at(k);
        std::string argDec = generateArgStr(curP);
        if(k!=0)
            this->Out<<",\n";
        else if(functionArgs.size()!=0)
            this->Out<<",\n";
        this->Out<<argDec;
    }

    /*****end****/
    this->Out<<")\n{\n";



    for(unsigned decInd = 0; decInd<partitionDecStr.size(); decInd++)
    {
        std::string curDec = partitionDecStr.at(decInd);
        this->Out<<curDec<<"\n";
    }

    bool encloseWhileLoop = false;
    if(li->getLoopDepth(domBB)!=0)
    {
        encloseWhileLoop = true;
    }
    if(encloseWhileLoop)
    {
        this->Out<<"while(1){\n";
    }
    for(unsigned int s=0; s < curPartitionBBStr.size(); s++)
    {
        std::vector<std::string>* curBBStr = curPartitionBBStr.at(s);
        BasicBlock* curBB = BBList->at(s);
        // now traverse it

        for(unsigned int k =0; k<curBBStr->size(); k++)
        {
            if(k==curBBStr->size()-1)
            {
                //FIXME: only when the last ins is terminator
                // when we do this phi assignment
                //need to do the phi thing
                if(phiPreAssign.find(curBB)!=phiPreAssign.end())
                {
                    std::vector<std::string>* allPhiStr = phiPreAssign[curBB];
                    for(unsigned phiInd = 0; phiInd < allPhiStr->size(); phiInd++)
                    {
                        errs()<<"              "<<allPhiStr->at(phiInd)<<"\n";
                        this->Out<<allPhiStr->at(phiInd)<<"\n";
                    }

                }
            }
            this->Out<<curBBStr->at(k);
        }
    }

    // now we output the endgroup
    this->Out<<endgroup;
    if(encloseWhileLoop)
    {
        this->Out<<"\n}\n";
    }
    this->Out<<"\n}\n";


    partGenId++;
    this->Out<<"//=========================================================================\n";
    // clear the functionArgs and fifoArgs vector
    for(unsigned k =0; k< functionArgs.size(); k++)
    {
        delete functionArgs.at(k);
    }
    for(unsigned k =0; k<fifoArgs.size();k++)
    {
        delete fifoArgs.at(k);
    }


}

// we have a data structure to map instruction to InstructionGraphNodes
// when we do the partitioning, we
bool PartitionGen::runOnFunction(Function &F) {

    curFunc = &F;

    int bbCount =0;

    for(Function::iterator bbi = F.begin(), bbe = F.end(); bbi!=bbe; ++bbi)
    {
        if(bbi->getName().size()==0)
        {
            std::string bbPrefix("BB_Explicit_");
            std::string bbIndStr = int2Str(bbCount);
            std::string newBbName = bbPrefix+bbIndStr;
            bbi->setName(newBbName);
            bbCount+=1;
        }
        else
        {
            std::string legal = replaceAllDotWUS(bbi->getName());

            bbi->setName(legal);
        }
    }

    /*for(Function::iterator bbi = F.begin(), bbe = F.end(); bbi!=bbe; ++bbi)
    {
        errs()<<bbi->getName()<<"\n";
    }*/


    InstructionGraphNode* rootNode = getAnalysis<InstructionGraph>().getRoot();


    //unsigned sccNum = 0;
    std::vector<DAGNode4Partition*> collectedDagNode;

    errs() << "SCCs for the program in PostOrder:"<<F.getName();
    for (scc_iterator<InstructionGraphNode*> SCCI = scc_begin(rootNode),
           E = scc_end(rootNode); SCCI != E; ++SCCI) {

      std::vector<InstructionGraphNode*> &curSCC = *SCCI;
      // we can ignore the last scc coz its the pseudo node root
      if(curSCC.at(0)->getInstruction()!=0 )
      {
          DAGNode4Partition* curDagNode = new DAGNode4Partition;
          curDagNode->init();
          curDagNode->dagNodeContent = new std::vector<InstructionGraphNode*>();
          *(curDagNode->dagNodeContent) = curSCC;
          curDagNode->singleIns = (curSCC.size()==1);

          for (std::vector<InstructionGraphNode*>::const_iterator I = curSCC.begin(),
                 E = curSCC.end(); I != E; ++I)
          {
              Instruction* curIns = (*I)->getInstruction();
              curDagNode->sccLat += instructionLatencyLookup(curIns);
              if(curIns->mayReadOrWriteMemory())
              {

                  curDagNode->hasMemory = true;
              }

              DAGNode4Partition *&IGN = this->dagNodeMap[curIns];
              IGN = curDagNode;

          }
          collectedDagNode.push_back(curDagNode);
      }



      /*errs() << "\nSCC #" << ++sccNum << " : ";
      for (std::vector<InstructionGraphNode*>::const_iterator I = nextSCC.begin(),
             E = nextSCC.end(); I != E; ++I)
      {
        if((*I)->getInstruction())
            errs() << *((*I)->getInstruction())<< "\n ";
        if (nextSCC.size() == 1 && SCCI.hasLoop())
            errs() << " (Has self-loop).";
      }*/
    }
    //errs()<<"here\n\n\n\n\n\n";
    std::reverse(collectedDagNode.begin(),collectedDagNode.end());
    // now the nodes are topologically shorted
    // let's check
    for(unsigned int dnInd =0; dnInd < collectedDagNode.size(); dnInd++)
    {
        DAGNode4Partition* curNode = collectedDagNode.at(dnInd);
        curNode->seqNum = dnInd;
    }
    for(unsigned int dnInd =0; dnInd < collectedDagNode.size(); dnInd++)
    {
        DAGNode4Partition* curNode = collectedDagNode.at(dnInd);
        std::vector<DAGNode4Partition*> myDep;
        findDependentNodes(curNode,this->dagNodeMap,myDep);
        // check every dep to make sure their seqNum is greater
        //errs()<<"my seq "<<curNode->seqNum<<" : my deps are ";
        for(unsigned depInd = 0; depInd<myDep.size(); depInd++)
        {
            //errs()<<myDep.at(depInd)->seqNum<<" ,";
            if(myDep.at(depInd)->seqNum < curNode->seqNum)
            {
                errs()<<"not topologically sorted\n";
                exit(1);

            }

        }
        errs()<<"all scc nodes topologically sorted, continue\n";
    }
    // all instructions have been added to the dagNodeMap, collectedDagNode
    // we can start building dependencies in the DAGNodePartitions
    // we can start making the partitions
    generatePartition(&collectedDagNode);
    // now all partitions are made
    // for each of these partitions, we will generate a control flow
    // before generating c function
    errs()<<"before check\n";
    generateControlFlowPerPartition();



    dagNodeMap.clear();
    dagPartitionMap.clear();
    // empty all the partitions
    for(unsigned k = 0; k<partitions.size(); k++)
    {
        delete partitions.at(k);
    }
    partitions.clear();
    for(unsigned k =0; k<collectedDagNode.size();k++)
    {
        delete collectedDagNode.at(k);
    }

    errs() << "\n";







  return true;
}



#endif
