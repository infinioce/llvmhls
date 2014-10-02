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

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "InstructionGraph.h"
#include "generatePartitionsUtil.h"
#include "llvm/Analysis/Dominators.h"
#include <vector>
#include <queue>
#define LONGLATTHRESHOLD 5
using namespace llvm;

namespace {
    struct DAGNode4Partition;
    struct DAGPartition;
    typedef std::map<const Instruction *, DAGNode4Partition *> DagNodeMapTy;
    typedef std::map<const DAGNode4Partition*, DAGPartition *> DagPartitionMapTy;
    typedef std::map<BasicBlock*, std::vector<Instruction*>*> BBMap2Ins;
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
            std::vector<Instruction*>* curBBIns = map[bbKey];
            // now check if it is already included
            if(std::find(curBBIns->begin(),curBBIns->end(),curIns )== curBBIns->end())
            {
                curBBIns->push_back(curIns);
            }
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

    struct DAGPartition{
        // a partition contains a set of dagNode
        std::vector<DAGNode4Partition*> partitionContent;



        bool containMemory;
        bool containLongLatCyc;

        bool cycleDetectCovered;

        void init()
        {
            containMemory = false;
            containLongLatCyc = false;
            cycleDetectCovered = false;
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
        void addDagNode(DAGNode4Partition* dagNode,DagPartitionMapTy &nodeToPartitionMap )
        {
            dagNode->print();
            partitionContent.push_back(dagNode);
            dagNode->covered=true;
            containMemory |= dagNode->hasMemory;
            containLongLatCyc |= (dagNode->sccLat >= LONGLATTHRESHOLD);
            nodeToPartitionMap[dagNode] = this;
        }

        void generateBBList(DominatorTree* DT)
        {
            BBMap2Ins sourceBBs;
            BBMap2Ins insBBs;

            std::vector<Instruction*> srcInstructions;
            for(unsigned int nodeInd = 0; nodeInd < partitionContent.size(); nodeInd++)
            {
                DAGNode4Partition* curNode = partitionContent.at(nodeInd);
                for(unsigned int insInd = 0; insInd< curNode->dagNodeContent->size(); insInd++)
                {
                    Instruction* curIns = curNode->dagNodeContent->at(insInd)->getInstruction();
                    addBBInsToMap(insBBs,curIns);
                    int numOp = curIns->getNumOperands();
                    for(unsigned int opInd = 0; opInd < numOp; opInd++)
                    {
                        Value* curOp = curIns->getOperand(opInd);
                        if(isa<Instruction>(*curOp))
                        {
                            Instruction* srcIns = &(cast<Instruction>(*curOp));
                            srcInstructions.push_back(srcIns);
                            addBBInsToMap(sourceBBs,srcIns);
                        }
                    }
                }
            }

            // now make the instructions

            // dump out the bbs
            BasicBlock* prevBB=0;
            for(BBMapIter bmi = sourceBBs.begin(), bme = sourceBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                errs()<<curBB->getName()<<"\n";
                // now print out the source

                if(prevBB!=0)
                {
                    BasicBlock* com = DT->findNearestCommonDominator(prevBB, curBB);
                    errs()<<"\n"<<com->getName()<<"is the com anc of "<<prevBB->getName()<<" and "<<curBB->getName()<<"\n";
                    prevBB = com;
                }
                else
                    prevBB = curBB;
            }
            errs()<<"done part\n";
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
    static void findDependentNodes(DAGNode4Partition* curNode, DagNodeMapTy &nodeLookup, std::vector<DAGNode4Partition*> &depNodes)
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

  struct PartitionGen : public FunctionPass {
    static char ID;  // Pass identification, replacement for typeid

    //std::vector<DAGNode4Partition*> allDAGNodes;


    std::vector<DAGPartition*> partitions;

    // from instruction to node
    DagNodeMapTy dagNodeMap;
    // from node to partition
    DagPartitionMapTy dagPartitionMap;

    PartitionGen() : FunctionPass(ID) {}
    bool runOnFunction(Function& func);

    void generatePartition(std::vector<DAGNode4Partition*> *dag);

    void generateControlFlowPerPartition();
    void print(raw_ostream &O, const Module* = 0) const { }

    void DFSCluster(DAGNode4Partition* curNode, DAGPartition* curPartition);
    void BFSCluster(DAGNode4Partition* curNode);
    void BarrierCluster(std::vector<DAGNode4Partition*> *dag);
    bool DFSFindPartitionCycle(DAGPartition* nextHop);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<InstructionGraph>();
      AU.addRequired<DominatorTree>();
      AU.setPreservesAll();
    }




  };

}

char PartitionGen::ID = 0;
static RegisterPass<PartitionGen>
Y("dpp-gen", "generate decoupled processing functions for each function CFG");


bool compareDagNode(DAGNode4Partition* first, DAGNode4Partition* second)
{
    return first->seqNum < second->seqNum;
}

void PartitionGen::BarrierCluster(std::vector<DAGNode4Partition*> *dag)
{
    DAGPartition* curPartition = new DAGPartition;
    curPartition->init();
    partitions.push_back(curPartition);

    for(unsigned int dagInd = 0; dagInd < dag->size(); dagInd++)
    {

        DAGNode4Partition* curDagNode = dag->at(dagInd);
        if(needANewPartition(curPartition,curDagNode))
        {
            curPartition = new DAGPartition;
            curPartition->init();
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
    curPartition->init();
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
                    curPartition->init();
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
    // try to see what are the seq
    for(unsigned int j = 0; j<depNodes.size(); j++)
    {
        errs()<<depNodes.at(j)->seqNum<<" ";
    }
    errs()<<"\n";
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

    errs()<<"#of partitions :"<<partitions.size()<<"\n";
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
    }
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
    for(unsigned int partInd = 0; partInd < partitions.size(); partInd++)
    {
        DAGPartition* curPart = partitions.at(partInd);
        DominatorTree* DT  = getAnalysisIfAvailable<DominatorTree>();
        curPart->generateBBList(DT);


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

            DAGPartition* nextHop = dagPartitionMap[nextNode];
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

// we have a data structure to map instruction to InstructionGraphNodes
// when we do the partitioning, we
bool PartitionGen::runOnFunction(Function &F) {


    InstructionGraphNode* rootNode = getAnalysis<InstructionGraph>().getRoot();


    unsigned sccNum = 0;
    std::vector<DAGNode4Partition*> collectedDagNode;

    errs() << "SCCs for the program in PostOrder:"<<F.getName();
    for (scc_iterator<InstructionGraphNode*> SCCI = scc_begin(rootNode),
           E = scc_end(rootNode); SCCI != E; ++SCCI) {

      std::vector<InstructionGraphNode*> &nextSCC = *SCCI;
      // we can ignore the last scc coz its the pseudo node root
      if(nextSCC.at(0)->getInstruction()!=0 )
      {
          DAGNode4Partition* curDagNode = new DAGNode4Partition;
          curDagNode->init();
          curDagNode->dagNodeContent = new std::vector<InstructionGraphNode*>();
          *(curDagNode->dagNodeContent) = nextSCC;
          curDagNode->singleIns = (nextSCC.size()==1);

          for (std::vector<InstructionGraphNode*>::const_iterator I = nextSCC.begin(),
                 E = nextSCC.end(); I != E; ++I)
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



      errs() << "\nSCC #" << ++sccNum << " : ";
      for (std::vector<InstructionGraphNode*>::const_iterator I = nextSCC.begin(),
             E = nextSCC.end(); I != E; ++I)
      {
        if((*I)->getInstruction())
            errs() << *((*I)->getInstruction())<< "\n ";
        if (nextSCC.size() == 1 && SCCI.hasLoop())
            errs() << " (Has self-loop).";
      }
    }
    errs()<<"here\n\n\n\n\n\n";
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
        errs()<<"my seq "<<curNode->seqNum<<" : my deps are ";
        for(unsigned depInd = 0; depInd<myDep.size(); depInd++)
        {
            errs()<<myDep.at(depInd)->seqNum<<" ,";
            if(myDep.at(depInd)->seqNum < curNode->seqNum)
            {
                errs()<<"not topologically sorted\n";
                exit(1);

            }

        }
        errs()<<"\n";
    }





   /* for(unsigned int m=0; m <collectedDagNode.size(); m++)
    {
        //errs()<<m<<"node\n";
        DAGNode4Partition* curNode = collectedDagNode.at(m);
        const std::vector<InstructionGraphNode*> curNodeContent = *(curNode->dagNodeContent);
        //errs()<<curNodeContent.size()<<"\n";
        for (unsigned int mi=0; mi<curNodeContent.size();mi++)
        {
            if((curNodeContent.at(mi))->getInstruction())
              errs() << *((curNodeContent.at(mi))->getInstruction())<< "\n ";

        }
    }*/


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

