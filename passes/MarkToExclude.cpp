/**
 * ************************************************************************************************
 * @brief  LLVM pass implementing globals duplication for EDDI (see EDDI.cpp).
 * 
 * @author Davide Baroffio, Politecnico di Milano, Italy (davide.baroffio@polimi.it)
 * ************************************************************************************************
*/
#include "ASPIS.h"
#include "Utils/Utils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Debug.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <llvm/Support/CommandLine.h>
#include <map>
#include <list>
#include <unordered_set>
#include <queue>
#include <iostream>
#include <fstream>
using namespace llvm;

#define DEBUG_TYPE "Mark_to_exclude"

/**
 * attribute exclude: do not duplicate.
 */

/**
 * @param Md
 * @return
 */
PreservedAnalyses MarkToExclude::run(Module &Md, ModuleAnalysisManager &AM) {
  LLVM_DEBUG(dbgs() << "Preprocessing " << Md.getName() << "...\n");

  // Replace all uses of aliases to aliasees
  for (auto &alias : Md.aliases()) {
    auto aliasee = alias.getAliaseeObject();
    if(isa<Function>(aliasee)){
      alias.replaceAllUsesWith(aliasee);
    }
  }

  std::map<Value*, StringRef> FuncAnnotations;
  getFuncAnnotations(Md, FuncAnnotations);

  // Create the annotation string as a global constant.
  Constant *AnnotationString = ConstantDataArray::getString(Md.getContext(), "exclude", true);
  GlobalVariable *AnnotationStringGlobal = new GlobalVariable(
      Md,
      AnnotationString->getType(),
      true,
      GlobalValue::PrivateLinkage,
      AnnotationString,
      ".str.annotation.exclude");
  AnnotationStringGlobal->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  for (GlobalVariable &GV : Md.globals()) {
    bool isReservedName = GV.getName().starts_with("llvm.");
    if(GV.isStrongDefinitionForLinker()){
      bool toExclude = FuncAnnotations.find(&GV) == FuncAnnotations.end() ||
                      (!FuncAnnotations.find(&GV)->second.startswith("exclude") &&
                      !FuncAnnotations.find(&GV)->second.startswith("to_duplicate"));
      
      if(!isReservedName && toExclude){
        LLVM_DEBUG(dbgs() << "Excluding " << GV.getName() << "\n");
        addAnnotation(Md, GV, AnnotationStringGlobal);
      }
    }
  }

  for (Function &Fn : Md) {
    if(Fn.isStrongDefinitionForLinker()){
      bool toExclude = FuncAnnotations.find(&Fn) == FuncAnnotations.end() ||
                      (!FuncAnnotations.find(&Fn)->second.startswith("exclude") && 
                      !FuncAnnotations.find(&Fn)->second.startswith("to_duplicate"));

      if(toExclude){
        LLVM_DEBUG(dbgs() << "Excluding " << Fn.getName() << "\n");
        addAnnotation(Md, Fn, AnnotationStringGlobal);
      }
    }
  }

  return PreservedAnalyses::none();
}


//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getMarkToExcludePluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "mark-to-exclude", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mark-to-exclude") {
                    FPM.addPass(MarkToExclude());
                    return true;
                  }
                  return false;
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize HelloWorld when added to the pass pipeline on the
// command line, i.e. via '-passes=hello-world'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getMarkToExcludePluginInfo();
}