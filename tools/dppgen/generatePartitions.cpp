//===- PrintSCC.cpp - Enumerate SCCs in some key graphs -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides passes to print out SCCs in a CFG or a CallGraph.
// Normally, you would not use these passes; instead, you would use the
// scc_iterator directly to enumerate SCCs and process them in some way.  These
// passes serve three purposes:
//
// (1) As a reference for how to use the scc_iterator.
// (2) To print out the SCCs for a CFG or a CallGraph:
//       analyze -print-cfg-sccs            to print the SCCs in each CFG of a module.
//       analyze -print-cfg-sccs -stats     to print the #SCCs and the maximum SCC size.
//       analyze -print-cfg-sccs -debug > /dev/null to watch the algorithm in action.
//
//     and similarly:
//       analyze -print-callgraph-sccs [-stats] [-debug] to print SCCs in the CallGraph
//
// (3) To test the scc_iterator.
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
#include <vector>
#define LONGLATTHRESHOLD 5
using namespace llvm;

namespace {
    struct DAGNode4Partition;
    struct DAGPartition;
    typedef std::map<const Instruction *, DAGNode4Partition *> DagNodeMapTy;
    typedef std::map<const DAGNode4Partition*, DAGPartition *> DagPartitionMapTy;



    struct DAGNode4Partition{
        std::vector<InstructionGraphNode*>* dagNodeContent;
        bool singleIns;
        int sccLat;
        bool hasMemory;
        bool covered;

        void init()
        {
            singleIns = false;
            sccLat = 0;
            hasMemory = false;
            covered = false;
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

        void init()
        {
            containMemory = false;
            containLongLatCyc = false;
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

    void print(raw_ostream &O, const Module* = 0) const { }

    void DFSCluster(DAGNode4Partition* curNode, DAGPartition* curPartition);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<InstructionGraph>();
      AU.setPreservesAll();
    }




  };

}

char PartitionGen::ID = 0;
static RegisterPass<PartitionGen>
Y("dpp-gen", "generate decoupled processing functions for each function CFG");


void PartitionGen::DFSCluster(DAGNode4Partition* curNode, DAGPartition* curPartition)
{

    if(needANewPartition(curPartition,curNode))
        return;
    else
        curPartition->addDagNode(curNode,  dagPartitionMap);

    // now we shall figure out the next hop
    std::vector<DAGNode4Partition*> depNodes;
    findDependentNodes(curNode, dagNodeMap,depNodes);
    for(unsigned int j = 0; j<depNodes.size(); j++)
    {
        DAGNode4Partition* nextNode = depNodes.at(j);
        if(nextNode->covered==false)
            DFSCluster(nextNode, curPartition);
    }
}

void PartitionGen::generatePartition(std::vector<DAGNode4Partition*> *dag)
{
    DAGPartition* curPartition = 0;


    for(unsigned int dagInd = 0; dagInd < dag->size(); dagInd++)
    {
        DAGNode4Partition* curNode = dag->at(dagInd);
        if(curNode->covered)
            continue;

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

        DFSCluster(curNode, curPartition);


    }

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

// we have a data structure to map instruction to InstructionGraphNodes
// when we do the partitioning, we
bool PartitionGen::runOnFunction(Function &F) {


    InstructionGraphNode* rootNode = getAnalysis<InstructionGraph>().getRoot();


    //unsigned sccNum = 0;
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
                  curDagNode->hasMemory = true;

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
    errs()<<"here\n\n\n\n\n\n";
    std::reverse(collectedDagNode.begin(),collectedDagNode.end());
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

    errs() << "\n";







  return true;
}

