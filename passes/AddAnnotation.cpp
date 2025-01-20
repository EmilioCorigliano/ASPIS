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

#define DEBUG_TYPE "Add_annotation"

/**
 * attributes:
 * - exclude: do not duplicate;
 * - to_duplicate: call two times this function
 * - to_harden: Protect this and all the called functions 
 */

/**
 * @param Md
 * @return
 */
PreservedAnalyses AddAnnotation::run(Module &Md, ModuleAnalysisManager &AM) {
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
  Constant *AnnotationString = ConstantDataArray::getString(Md.getContext(), annotation, true);
  GlobalVariable *AnnotationStringGlobal = new GlobalVariable(
      Md,
      AnnotationString->getType(),
      true,
      GlobalValue::PrivateLinkage,
      AnnotationString,
      ".str.annotation."+annotation);
  AnnotationStringGlobal->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  for (GlobalVariable &GV : Md.globals()) {
    bool isReservedName = GV.getName().starts_with("llvm.");
    if(GV.isStrongDefinitionForLinker()){
      bool toAnnotate = FuncAnnotations.find(&GV) == FuncAnnotations.end() ||
                      (!FuncAnnotations.find(&GV)->second.startswith("exclude") &&
                      !FuncAnnotations.find(&GV)->second.startswith("to_duplicate") &&
                      !FuncAnnotations.find(&GV)->second.startswith("to_harden"));
      
      if(!isReservedName && toAnnotate){
        LLVM_DEBUG(dbgs() << "Annotating " << GV.getName() << " with " << annotation << "\n");
        addAnnotation(Md, GV, AnnotationStringGlobal, FuncAnnotations);
      }
    }
  }

  for (Function &Fn : Md) {
    if(Fn.isStrongDefinitionForLinker()){
      bool toAnnotate = FuncAnnotations.find(&Fn) == FuncAnnotations.end() ||
                      (!FuncAnnotations.find(&Fn)->second.startswith("exclude") && 
                      !FuncAnnotations.find(&Fn)->second.startswith("to_duplicate") &&
                      !FuncAnnotations.find(&Fn)->second.startswith("to_harden"));

      if(toAnnotate){
        LLVM_DEBUG(dbgs() << "Excluding " << Fn.getName() << "\n");
        addAnnotation(Md, Fn, AnnotationStringGlobal, FuncAnnotations);
      }
    }
  }

  return PreservedAnalyses::none();
}


//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getAddAnnotationPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "add-annotation", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mark-to-exclude") {
                    FPM.addPass(AddAnnotation("exclude"));
                    return true;
                  }
                  return false;
                });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mark-to-duplicate") {
                    FPM.addPass(AddAnnotation("to_duplicate"));
                    return true;
                  }
                  return false;
                });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mark-to-harden") {
                    FPM.addPass(AddAnnotation("to_harden"));
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
  return getAddAnnotationPluginInfo();
}