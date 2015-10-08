//===-- BuildSystemCommand.cpp --------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Commands/Commands.h"

#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildSystem.h"
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/BuildSystem/BuildValue.h"

#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "CommandUtil.h"

#include <cerrno>

using namespace llbuild;
using namespace llbuild::commands;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

namespace {

/*  Parse Command */

class ParseBuildFileDelegate : public BuildFileDelegate {
  bool showOutput;
  StringRef bufferBeingParsed;
  
public:
  ParseBuildFileDelegate(bool showOutput) : showOutput(showOutput) {}
  ~ParseBuildFileDelegate() {}

  virtual bool shouldShowOutput() { return showOutput; }
  
  virtual void setFileContentsBeingParsed(StringRef buffer) override;
  
  virtual void error(const std::string& filename,
                     const Token& at,
                     const std::string& message) override;

  virtual bool configureClient(const std::string& name,
                               uint32_t version,
                               const property_list_type& properties) override;

  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) override;

  virtual void loadedTarget(const std::string& name,
                            const Target& target) override;

  virtual std::unique_ptr<Node> lookupNode(const std::string& name,
                                           bool isImplicit) override;

  virtual void loadedCommand(const std::string& name,
                             const Command& command) override;
};

class ParseDummyNode : public Node {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyNode(ParseBuildFileDelegate& delegate, const std::string& name)
      : Node(name), delegate(delegate) {}
  
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.c_str(), value.c_str());
    }
    return true;
  }
};

class ParseDummyCommand : public Command {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyCommand(ParseBuildFileDelegate& delegate, const std::string& name)
      : Command(name), delegate(delegate) {}

  virtual void configureDescription(const std::string& description) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- 'description': '%s'", description.c_str());
    }
  }
  
  virtual void configureInputs(const std::vector<Node*>& inputs) override {
    if (delegate.shouldShowOutput()) {
      bool first = true;
      printf("  -- 'inputs': [");
      for (const auto& node: inputs) {
        printf("%s'%s'", first ? "" : ", ", node->getName().c_str());
        first = false;
      }
      printf("]\n");
    }
  }

  virtual void configureOutputs(const std::vector<Node*>& outputs) override {
    if (delegate.shouldShowOutput()) {
      bool first = true;
      printf("  -- 'outputs': [");
      for (const auto& node: outputs) {
        printf("%s'%s'", first ? "" : ", ", node->getName().c_str());
        first = false;
      }
      printf("]\n");
    }
  }

  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.c_str(), value.c_str());
    }
    return true;
  }

  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) override {
    return BuildValue::makeMissingInput();
  }
  virtual bool isResultValid(const BuildValue&) override { return false; }
  virtual void start(BuildSystemCommandInterface&, Task*) override {}
  virtual void providePriorValue(BuildSystemCommandInterface&, Task*,
                                 const BuildValue&) override {}
  virtual void provideValue(BuildSystemCommandInterface&, Task*,
                                 uintptr_t inputID,
                                 const BuildValue&) override {}
  virtual void inputsAvailable(BuildSystemCommandInterface&, Task*) override {}
};

class ParseDummyTool : public Tool {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyTool(ParseBuildFileDelegate& delegate, const std::string& name)
      : Tool(name), delegate(delegate) {}
  
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.c_str(), value.c_str());
    }
    return true;
  }

  virtual std::unique_ptr<Command> createCommand(
      const std::string& name) override {
    if (delegate.shouldShowOutput()) {
      printf("command('%s')\n", name.c_str());
      printf("  -- 'tool': '%s')\n", getName().c_str());
    }

    return std::make_unique<ParseDummyCommand>(delegate, name);
  }
};

void ParseBuildFileDelegate::setFileContentsBeingParsed(StringRef buffer) {
  bufferBeingParsed = buffer;
}

void ParseBuildFileDelegate::error(const std::string& filename,
                                   const Token& at,
                                   const std::string& message) {
  if (at.start) {
    util::emitError(filename, message, at.start, at.length, bufferBeingParsed);
  } else {
    fprintf(stderr, "%s: error: %s\n", filename.c_str(), message.c_str());
  }
}

bool
ParseBuildFileDelegate::configureClient(const std::string& name,
                                        uint32_t version,
                                        const property_list_type& properties) {
  if (showOutput) {
    // Dump the client information.
    printf("client ('%s', version: %u)\n", name.c_str(), version);
    for (const auto& property: properties) {
      printf("  -- '%s': '%s'\n", property.first.c_str(),
             property.second.c_str());
    }
  }

  return true;
}

std::unique_ptr<Tool>
ParseBuildFileDelegate::lookupTool(const std::string& name) {
  if (showOutput) {
    printf("tool('%s')\n", name.c_str());
  }

  return std::make_unique<ParseDummyTool>(*this, name);
}

void ParseBuildFileDelegate::loadedTarget(const std::string& name,
                                          const Target& target) {
  if (showOutput) {
    printf("target('%s')\n", target.getName().c_str());

    // Print the nodes in the target.
    bool first = true;
    printf(" -- nodes: [");
    for (const auto& node: target.getNodes()) {
      printf("%s'%s'", first ? "" : ", ", node->getName().c_str());
      first = false;
    }
    printf("]\n");
  }
}

std::unique_ptr<Node>
ParseBuildFileDelegate::lookupNode(const std::string& name,
                                   bool isImplicit) {
  if (!isImplicit) {
    if (showOutput) {
      printf("node('%s')\n", name.c_str());
    }
  }

  return std::make_unique<ParseDummyNode>(*this, name);
}

void ParseBuildFileDelegate::loadedCommand(const std::string& name,
                                        const Command& command) {
  if (showOutput) {
    printf("  -- -- loaded command('%s')\n", command.getName().c_str());
  }
}

static void parseUsage(int exitCode) {
  int optionWidth = 20;
  fprintf(stderr, "Usage: %s buildsystem parse [options] <path>\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--no-output",
          "don't display parser output");
  ::exit(exitCode);
}

static int executeParseCommand(std::vector<std::string> args) {
  bool showOutput = true;
  
  while (!args.empty() && args[0][0] == '-') {
    const std::string option = args[0];
    args.erase(args.begin());

    if (option == "--")
      break;

    if (option == "--help") {
      parseUsage(0);
    } else if (option == "--no-output") {
      showOutput = false;
    } else {
      fprintf(stderr, "\error: %s: invalid option: '%s'\n\n",
              getProgramName(), option.c_str());
      parseUsage(1);
    }
  }

  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getProgramName());
    parseUsage(1);
  }

  std::string filename = args[0].c_str();

  // Load the BuildFile.
  fprintf(stderr, "note: parsing '%s'\n", filename.c_str());
  ParseBuildFileDelegate delegate(showOutput);
  BuildFile buildFile(filename, delegate);
  buildFile.load();

  return 0;
}


/* Build Command */

class BasicBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
public:
  BasicBuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                                   const BuildSystemInvocation& invocation)
      : BuildSystemFrontendDelegate(sourceMgr, invocation,
                                    "basic", /*version=*/0) {}
  
  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) override {
    // We do not support any non-built-in tools.
    return nullptr;
  }
};

static void buildUsage(int exitCode) {
  int optionWidth = 25;
  fprintf(stderr, "Usage: %s buildsystem build [options] [<target>]\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  BuildSystemInvocation::getUsage(optionWidth, llvm::errs());
  ::exit(exitCode);
}

static int executeBuildCommand(std::vector<std::string> args) {
  // The source manager to use for diagnostics.
  llvm::SourceMgr sourceMgr;

  // Create the invocation.
  BuildSystemInvocation invocation{};

  // Initialize defaults.
  invocation.dbPath = "build.db";
  invocation.buildFilePath = "build.llbuild";
  invocation.parse(args, sourceMgr);

  // Handle invocation actions.
  if (invocation.showUsage) {
    buildUsage(0);
  } else if (invocation.hadErrors) {
    buildUsage(1);
  }
  
  if (invocation.positionalArgs.size() > 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    buildUsage(1);
  }

  // Select the target to build.
  std::string targetToBuild =
    invocation.positionalArgs.empty() ? "" : invocation.positionalArgs[0];

  // Create the frontend object.
  BasicBuildSystemFrontendDelegate delegate(sourceMgr, invocation);
  BuildSystemFrontend frontend(delegate, invocation);
  if (!frontend.build(targetToBuild)) {
    return 1;
  }

  return 0;
}

}

#pragma mark - Build System Top-Level Command

static void usage(int exitCode) {
  fprintf(stderr, "Usage: %s buildsystem [--help] <command> [<args>]\n",
          getProgramName());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  parse         -- Parse a build file\n");
  fprintf(stderr, "  build         -- Build using a build file\n");
  fprintf(stderr, "\n");
  exit(exitCode);
}

int commands::executeBuildSystemCommand(const std::vector<std::string> &args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (args.empty() || args[0] == "--help")
    usage(0);

  if (args[0] == "parse") {
    return executeParseCommand({args.begin()+1, args.end()});
  } else if (args[0] == "build") {
    return executeBuildCommand({args.begin()+1, args.end()});
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getProgramName(),
            args[0].c_str());
    usage(1);
    return 1;
  }
}
