#ifndef GENERATECINS
#define GENERATECINS

#include "llvm/Support/IncludeFile.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "InstructionGraph.h"
#include "generatePartitionsUtil.h"
#include <string>
#define ENDBLOCK "END"

typedef std::map<BasicBlock*, std::vector<std::string>*> BBMap2outStr;

std::string generateVariableName(Instruction* ins, int seqNum)
{
    std::string rtVarName= ins->getParent()->getName();

    rtVarName = rtVarName+int2Str(seqNum);
    return rtVarName;
}

int getInstructionSeqNum(Instruction* ins)
{
    BasicBlock* BB=ins->getParent();
    int seqNum = -1;
    for(BasicBlock::iterator insPt = BB->begin(), insEnd = BB->end(); insPt != insEnd; insPt++)
    {
        seqNum++;
        if( ins == insPt)
            break;
    }
    return seqNum;
}

std::string generateVariableName(Instruction* ins)
{
    int seqNum = getInstructionSeqNum(ins);
    return generateVariableName(ins, seqNum);
}
std::string generateGetElementPtrInstVarDec(Instruction& ins)
{
    std::string varType;
    if(!isa<GetElementPtrInst>(ins))
    {
        errs()<<"returning pointer type but not getele\n";
        exit(1);
    }
    GetElementPtrInst& gepi = cast<GetElementPtrInst>(ins);
    SmallVector<Value*, 16> Idxs(gepi.idx_begin(), gepi.idx_end());
    Type *ElTy = GetElementPtrInst::getIndexedType(gepi.getPointerOperandType(), Idxs);
    // now check what is it refered to
    switch(ElTy->getTypeID())
    {
        case Type::IntegerTyID:
            varType="int* ";
            break;
        case Type::FloatTyID:
            varType="float* ";
            break;
        case Type::DoubleTyID:
            varType ="double* ";
            break;
        case Type::HalfTyID:
            varType="short* ";
            break;
        //FIXME: need to recursively get the pointer type?
        case Type::PointerTyID:
        default:
            varType ="void* ";

    }
    return varType;
}



std::string generateVariableType(Instruction* ins)
{
    std::string varType;
    if(ins->isTerminator()&&!isa<ReturnInst>(*ins))
    {
        varType = "char";
        return varType;

    }
    switch(ins->getType()->getTypeID())
    {
        case Type::VoidTyID:
            varType="";
            break;
        case Type::HalfTyID:
            varType="short ";
            break;
        case Type::FloatTyID:
            varType="float ";
            break;
        case Type::DoubleTyID:
            varType ="double ";
            break;
        //X86_FP80TyID,    ///<  4: 80-bit floating point type (X87)
        //FP128TyID,       ///<  5: 128-bit floating point type (112-bit mantissa)
        //PPC_FP128TyID,   ///<  6: 128-bit floating point type (two 64-bits, PowerPC)
        //LabelTyID,       ///<  7: Labels
        //MetadataTyID,    ///<  8: Metadata
        //X86_MMXTyID,     ///<  9: MMX vectors (64 bits, X86 specific)

    // Derived types... see DerivedTypes.h file.
    // Make sure FirstDerivedTyID stays up to date!
        case Type::IntegerTyID:     ///< 10: Arbitrary bit width integers
            varType ="int ";
            break;
        //FunctionTyID,    ///< 11: Functions
        //StructTyID,      ///< 12: Structures
        //ArrayTyID,       ///< 13: Arrays
        case Type::PointerTyID:
        // well this is likely a getElementPtr thing
        // we need to find out what type it is pointing to
            varType = generateGetElementPtrInstVarDec(*ins);

            break;
        //VectorTyID,      ///< 15: SIMD 'packed' format, or other vector type
        default:
            errs()<<"unhandled type, exit\n";
            exit(1);
    }

}
std::string generatePushOp(std::string varName, std::string channelName)
{
    std::string rtStr;
    rtStr=rtStr+"push ("+channelName+","+varName+");\n";
    return rtStr;
}

std::string generateVariableStr(Instruction* ins, int seqNum)
{

    std::string rtVarName = generateVariableName(ins,seqNum);

    std::string varType=generateVariableType(ins);
    if(varType.length()==0)
        rtVarName="";
    std::string rtStr;
    rtStr = varType+" ";
    rtStr = rtStr +rtVarName+";";
    return rtStr;
}

std::string generateChannelString(int type, int seqNum, std::string source)
{                                           // seqNum denotes which instruction it is in the BB
    // type 0, branch tage channel
    // type 1, data channel
    std::string channelTypeStr;
    if(type == 0)
        channelTypeStr = "brTag";
    else
        channelTypeStr = "data";


    std::string final = channelTypeStr+int2Str(seqNum)+source;
    return final;
}

std::string generateOperandStr(Value* operand)
{
    std::string rtStr;
    if(isa<Instruction>(*operand))
    {
        Instruction& curIns = cast<Instruction>(*operand);
        // got to generate the varname
        int seqNum = getInstructionSeqNum(&curIns);
        std::string varName = generateVariableName(&curIns,seqNum);
        rtStr = varName;
    }
    else
    {
        rtStr = operand->getName();
    }
    return rtStr;
}

std::string generateGenericSwitchStatement(std::string varName,bool explicitCase, std::vector<std::string>* caseVal,
                                           std::vector<std::string>* tgtLabel,std::string defaultDest,
                                           bool remoteDst=false,std::string channelName="", unsigned int defaultSeq=0)
{
    assert((!explicitCase)||(caseVal->size()==tgtLabel->size()));
    std::string rtStr="";
    rtStr+= rtStr+"switch("+varName+")\n";
    rtStr+="{\n";

    unsigned int successorSeqNum = 0;
    for(unsigned int sucCount=0; sucCount<tgtLabel->size(); sucCount++ )
    {

        if(successorSeqNum ==defaultSeq)
            successorSeqNum++;
        std::string curCaseNum = explicitCase? caseVal->at(sucCount):int2Str(sucCount);
        rtStr+="\tcase "+curCaseNum+":\n";
        if(remoteDst)
        {
            // we need to push something to the channel
            // this should be which target it is
            rtStr+="\t";
            rtStr+=generatePushOp(int2Str(successorSeqNum),channelName);


        }
        rtStr+="\tgoto "+tgtLabel->at(sucCount)+";\n";
        successorSeqNum++;

    }
    rtStr+="default:\n";
    if(remoteDst)
    {
        rtStr+="\t";
        rtStr+=generatePushOp(int2Str(defaultSeq),channelName);
    }
    rtStr+="\tgoto "+defaultDest+ ";}\n";
    return rtStr;
}

std::string generateGettingRemoteBranchTag(TerminatorInst& curIns, int seqNum)
{
    std::string rtStr="";
    int channelType =  0;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    unsigned numSuc = curIns.getNumSuccessors();
    assert(numSuc < 255 && numSuc>0);

    // we need to get remote target tag
    rtStr = rtStr+"pop("+channelStr+","+varName+");\n";
    // if this is a unconditional branch we just do it
    if(numSuc==1)
    {
        BasicBlock* curSuc = curIns.getSuccessor(0);
        std::string sucName =  curSuc->getName();
        rtStr= rtStr+"goto ";
        rtStr= rtStr+ sucName;
        rtStr = rtStr +";\n";
        return rtStr;
    }
    std::vector<std::string> allTgt;
    for(unsigned int sucCount=0; sucCount<numSuc; sucCount++ )
    {
        BasicBlock* curSuc = curIns.getSuccessor(sucCount);
        allTgt.push_back(   curSuc->getName() );
    }
    rtStr = rtStr+generateGenericSwitchStatement(varName,0,0,&allTgt,ENDBLOCK);
    return rtStr;

}
std::string generateGettingRemoteData(Instruction& curIns, int seqNum)
{
    std::string rtStr="";
    int channelType =  1;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    rtStr = rtStr+"pop("+channelStr+","+varName+");\n";
    return rtStr;
}
std::string generateLoadInstruction(LoadInst& li, std::string varName)
{

    Value* ptrVal = li.getPointerOperand();
    std::string ptrStr = generateOperandStr(ptrVal);
    return varName+"= *("+ptrStr+");\n";
}
std::string generateStoreInstruction(StoreInst& si )
{

    Value* ptrVal = si.getPointerOperand();
    Value* val = si.getValueOperand();
    std::string ptrStr = generateOperandStr(ptrVal);
    std::string valStr = generateOperandStr(val);
    std::string rtStr ="*("+ptrStr+") = "+valStr+";\n";
    return rtStr;
}
std::string generateGetElementPtrInstruction(GetElementPtrInst& gepi, std::string varName)
{

    Value* ptr = gepi.getPointerOperand();
    std::string ptrStr = generateOperandStr( ptr);

    std::string offSetStr = generateOperandStr(gepi.getOperand(1));
    // check the index array and do additions
    std::string rtStr = varName+"= "+ptrStr+"+"+offSetStr+";\n";
    return rtStr;
}
std::string generateEndBlock(std::vector<BasicBlock*>* BBList )
{
    std::vector<BasicBlock*> outsideBBs;

    for(unsigned int bbInd = 0; bbInd < BBList->size(); bbInd++)
    {
        BasicBlock* curBB = BBList->at(bbInd);
        TerminatorInst* bTerm = curBB->getTerminator();
        for(unsigned int sucInd = 0; sucInd < bTerm->getNumSuccessors(); sucInd)
        {
            BasicBlock* curSuc = bTerm->getSuccessor(sucInd);
            if(std::find(BBList->begin(),BBList->end(),curSuc) == BBList->end()  )
            {
                // add the curSuc to outsideBBs if it is not already there
                if(std::find(outsideBBs.begin(),outsideBBs.end(),curSuc) == outsideBBs.end() )
                {
                    outsideBBs.push_back(curSuc);
                }
            }
        }
    }
    // now generate the endblock
    std::string rtStr=ENDBLOCK;
    rtStr += ":\n";
    for(unsigned int k = 0; k<outsideBBs.size(); k++)
    {
        rtStr += outsideBBs.at(k)->getName();
        rtStr += ":\n";

    }
    return rtStr;

}

std::string generateSelectOperations(SelectInst& curIns,bool remoteDst,int seqNum)
{
    std::string rtStr="";
    int channelType =  1;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    std::string condStr  = generateOperandStr( curIns.getCondition());
    std::string trueStr = generateOperandStr( curIns.getTrueValue());
    std::string falseStr = generateOperandStr( curIns.getFalseValue());
    rtStr+= varName+" = "+condStr+"?"+trueStr+":"+falseStr+";\n";
    if(remoteDst)
        rtStr=rtStr+generatePushOp(varName,channelStr);
    return rtStr;


}


std::string generatePhiNode(PHINode& curIns,bool remoteDst,int seqNum,
                                  BBMap2outStr* preAssign)
{
    std::string rtStr="";
    int channelType =  1;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    for(unsigned int inBlockInd = 0; inBlockInd<curIns.getNumIncomingValues();inBlockInd++)
    {
        BasicBlock* curPred = curIns.getIncomingBlock(inBlockInd);
        Value* curPredVal = curIns.getIncomingValueForBlock(curPred);
        // now we check if the BB has a vector of strings, if not we add one
        if(preAssign->find(curPred)==preAssign->end())
        {
            std::vector<std::string>*& predPhiStrings = (*preAssign)[curPred];
            predPhiStrings = new std::vector<std::string>();
        }
        // now we generate the string, its essentially,
        std::string valueStr = generateOperandStr(curPredVal);
        std::string preAssignStr = varName+"="+valueStr+";\n";
        ((*preAssign)[curPred])->push_back(preAssignStr);
    }
    if(remoteDst)
    {
        rtStr=rtStr+generatePushOp(varName,channelStr);
    }
    return rtStr;

}

std::string generateMemoryOperations(Instruction& curIns, bool remoteDst, int seqNum)
{
    std::string rtStr="";
    int channelType =  1;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    switch(curIns.getOpcode())
    {
    case Instruction::Alloca:
        // just generate a declaration
        rtStr = generateVariableStr(&curIns,seqNum);
        break;
    case Instruction::Load:
        //LoadInst& li = cast<LoadInst>(curIns);
        rtStr+=generateLoadInstruction(cast<LoadInst>(curIns),varName);
        break;
    case Instruction::Store:
        rtStr+=generateStoreInstruction(cast<StoreInst>(curIns));
        break;
    case Instruction::GetElementPtr:
        rtStr+=generateGetElementPtrInstruction( cast<GetElementPtrInst>(curIns), varName);
        break;
    default:
        errs()<<"unhandled instruction\n";
        exit(0);
    }
    if(remoteDst)
    {
        rtStr=rtStr+generatePushOp(varName,channelStr);
    }

    return rtStr;

}
std::string generateSimpleAssign(Instruction& curIns, std::string varName)
{
    std::string rtStr = varName;
    std::string originalVal = generateOperandStr( curIns.getOperand(0));
    rtStr += "=" + originalVal+";\n";
    return rtStr;
}


std::string generateCastOperations(Instruction& curIns, bool remoteDst, int seqNum)
{
    std::string rtStr="";
    int channelType =  1;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    switch(curIns.getOpcode())
    {
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::BitCast:
        // just generate a declaration
        rtStr += generateSimpleAssign(curIns,varName);
        break;
    default:
        errs()<<"unhandled cast instruction\n"<<curIns;
        exit(0);
    }
    if(remoteDst)
    {
        rtStr=rtStr+generatePushOp(varName,channelStr);
    }

    return rtStr;

}

std:: string generateBinaryOperations(BinaryOperator& curIns, bool remoteDst,int seqNum )
{
    std::string rtStr="";
    int channelType =  1;
    std::string channelStr = generateChannelString(channelType,seqNum,curIns.getParent()->getName());
    std::string varName = generateVariableName(&curIns,seqNum);
    // need to get the operand
    // they can be instruction, they can be functional argument or they can be
    // constant -- we need to get operand var name when
    // two operand
    Value* firstOperand = curIns.getOperand(0);
    Value* secondOperand = curIns.getOperand(1);
    std::string firstOperandStr = generateOperandStr(firstOperand);
    std::string secondOperandStr = generateOperandStr(secondOperand);

    // generate the actual computation
    switch(curIns.getOpcode())
    {
    case Instruction::Add:
    case Instruction::FAdd:
        rtStr = rtStr + firstOperandStr + "+";
        break;
    case Instruction::Sub:
    case Instruction::FSub:
        rtStr = rtStr + firstOperandStr + "-";
        break;
    case Instruction::Mul:
    case Instruction::FMul:
        rtStr = rtStr + firstOperandStr + "*";
        break;
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
        rtStr = rtStr + firstOperandStr + "/";
        break;
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
        rtStr = rtStr + firstOperandStr + "%";
        break;
    case Instruction::Shl:
        rtStr = rtStr + firstOperandStr + "<<";
        break;
    case Instruction::LShr:
    case Instruction::AShr:
        rtStr = rtStr + firstOperandStr + ">>";
        break;
    case Instruction::And:
        rtStr = rtStr + firstOperandStr + "&";
        break;
    case Instruction::Or:
        rtStr = rtStr + firstOperandStr + "|";
        break;
    case Instruction::Xor:
        rtStr = rtStr + firstOperandStr +"^";
        break;
    default:
        errs()<<"Unrecognized binary Ops\n";
        exit(1);

    }
    rtStr = rtStr + secondOperandStr+ ";\n";
    if(remoteDst)
    {
        rtStr=rtStr+generatePushOp(varName,channelStr);
    }
    return rtStr;
}

std::string generateControlFlow(TerminatorInst& curIns,bool remoteDst, int seqNum)
{
    // we currently deal with br and switch only
    assert(isa<BranchInst>(curIns) || isa<SwitchInst>(curIns) );
    std::string rtStr="";
    std::string channelName = generateChannelString(0, seqNum, curIns.getParent()->getName());
    if(isa<BranchInst>(curIns))
    {
        BranchInst& bi = cast<BranchInst>(curIns);
        std::string firstSucName= bi.getSuccessor(0)->getName();
        if(bi.isUnconditional())
        {
            if(remoteDst)
            {
                rtStr=rtStr+generatePushOp("1",channelName);
            }
            rtStr=rtStr+"goto ";

            rtStr=rtStr+firstSucName +";\n";
        }
        else
        {
            Value* condVal = bi.getCondition();
            assert(isa<Instruction>(*condVal));
            Instruction* valGenIns = &(cast<Instruction>(*condVal));
            std::string switchVar = generateVariableName(valGenIns);
            rtStr="if("+switchVar+"){\n";
            if(remoteDst)
                rtStr=rtStr+generatePushOp("0",channelName);
            rtStr=rtStr+"goto "+firstSucName+";}\n";

            rtStr=rtStr+"else{\n";
            if(remoteDst)
                rtStr=rtStr+generatePushOp("1",channelName);

            std::string secondSucName = bi.getSuccessor(1)->getName();
            rtStr=rtStr+"goto "+secondSucName+";}\n";

        }

    }
    else
    {
        // this is when its a switch
        // we need to build a set of potential destination/case values
        SwitchInst& si = cast<SwitchInst>(curIns);
        std::vector<std::string> caseVal;
        std::vector<std::string> allTgt;
        std::string defaultDest=ENDBLOCK;
        unsigned int defaultSeq = 0;
        for(unsigned int sucInd=0; sucInd < si.getNumSuccessors(); sucInd++)
        {
            BasicBlock* curBB = si.getSuccessor(sucInd);
            ConstantInt* curCaseVal = si.findCaseDest(curBB);
            if(curCaseVal==NULL)
            {
                defaultDest =  curBB->getName();
                defaultSeq=sucInd;
            }
            else
            {
                allTgt.push_back( curBB->getName());
                caseVal.push_back(curCaseVal->getValue().toString(10,true));
            }

        }
        std::string varName = generateVariableName(&curIns,seqNum);
        rtStr = generateGenericSwitchStatement(varName,true,&caseVal,&allTgt,defaultDest,true,channelName,defaultSeq);
    }
    return rtStr;

    // for branch, we can convert it to switch statement as well


}
std::string generateReturn(ReturnInst& curIns)
{
    std::string rtStr="return ";
    if(curIns.getReturnValue()->getType()->getTypeID()==Type::VoidTyID)
    {
        return rtStr+"void;\n";
    }
    else
    {
        // find the variable name
        Value* retVal = curIns.getReturnValue();
        assert(isa<Instruction>(*condVal));
        Instruction* valGenIns = &(cast<Instruction>(*retVal));
        std::string retVar = generateVariableName(valGenIns);
        rtStr=rtStr+retVar+";\n";
    }
}

#endif
