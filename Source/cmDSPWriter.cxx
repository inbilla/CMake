/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#include "cmDSPWriter.h"
#include "cmStandardIncludes.h"
#include "cmSystemTools.h"
#include "cmRegularExpression.h"

cmDSPWriter::~cmDSPWriter()
{
}


cmDSPWriter::cmDSPWriter(cmMakefile*mf)
{
  m_Makefile = mf;
}

void cmDSPWriter::OutputDSPFile()
{ 
  // If not an in source build, then create the output directory
  if(strcmp(m_Makefile->GetStartOutputDirectory(),
            m_Makefile->GetHomeDirectory()) != 0)
    {
    if(!cmSystemTools::MakeDirectory(m_Makefile->GetStartOutputDirectory()))
      {
      cmSystemTools::Error("Error creating directory ",
                           m_Makefile->GetStartOutputDirectory());
      }
    }

  // Setup /I and /LIBPATH options for the resulting DSP file
  std::vector<std::string>& includes = m_Makefile->GetIncludeDirectories();
  std::vector<std::string>::iterator i;
  for(i = includes.begin(); i != includes.end(); ++i)
    {
    m_IncludeOptions +=  " /I ";
    std::string tmp = cmSystemTools::ConvertToOutputPath(i->c_str());

    // quote if not already quoted
    if (tmp[0] != '"')
      {
      m_IncludeOptions += "\"";
      m_IncludeOptions += tmp;
      m_IncludeOptions += "\"";
      }
    else
      {
      m_IncludeOptions += tmp;
      }
    }
  
  // Create the DSP or set of DSP's for libraries and executables

  // clear project names
  m_CreatedProjectNames.clear();

  // build any targets
  cmTargets &tgts = m_Makefile->GetTargets();
  for(cmTargets::iterator l = tgts.begin(); 
      l != tgts.end(); l++)
    {
    switch(l->second.GetType())
      {
      case cmTarget::STATIC_LIBRARY:
        this->SetBuildType(STATIC_LIBRARY, l->first.c_str());
        break;
      case cmTarget::SHARED_LIBRARY:
        this->SetBuildType(DLL, l->first.c_str());
        break;
      case cmTarget::EXECUTABLE:
        this->SetBuildType(EXECUTABLE,l->first.c_str());
        break;
      case cmTarget::WIN32_EXECUTABLE:
        this->SetBuildType(WIN32_EXECUTABLE,l->first.c_str());
        break;
      case cmTarget::UTILITY:
        this->SetBuildType(UTILITY, l->first.c_str());
        break;
      case cmTarget::INSTALL_FILES:
	break;
      case cmTarget::INSTALL_PROGRAMS:
	break;
      default:
	cmSystemTools::Error("Bad target type", l->first.c_str());
	break;
      }
    // INCLUDE_EXTERNAL_MSPROJECT command only affects the workspace
    // so don't build a projectfile for it
    if ((l->second.GetType() != cmTarget::INSTALL_FILES)
        && (l->second.GetType() != cmTarget::INSTALL_PROGRAMS)
        && (strncmp(l->first.c_str(), "INCLUDE_EXTERNAL_MSPROJECT", 26) != 0))
      {
      this->CreateSingleDSP(l->first.c_str(),l->second);
      }
    }
}

void cmDSPWriter::CreateSingleDSP(const char *lname, cmTarget &target)
{
  // add to the list of projects
  std::string pname = lname;
  m_CreatedProjectNames.push_back(pname);
  // create the dsp.cmake file
  std::string fname;
  fname = m_Makefile->GetStartOutputDirectory();
  fname += "/";
  fname += lname;
  fname += ".dsp";
  // save the name of the real dsp file
  std::string realDSP = fname;
  fname += ".cmake";
  std::ofstream fout(fname.c_str());
  if(!fout)
    {
    cmSystemTools::Error("Error Writing ", fname.c_str());
    }
  this->WriteDSPFile(fout,lname,target);
  fout.close();
  // if the dsp file has changed, then write it.
  cmSystemTools::CopyFileIfDifferent(fname.c_str(), realDSP.c_str());
}


void cmDSPWriter::AddDSPBuildRule(cmSourceGroup& sourceGroup)
{
  std::string dspname = *(m_CreatedProjectNames.end()-1);
  if(dspname == "ALL_BUILD")
  {
    return;
  }
  dspname += ".dsp.cmake";
  std::string makefileIn = m_Makefile->GetStartDirectory();
  makefileIn += "/";
  makefileIn += "CMakeLists.txt";
  makefileIn = cmSystemTools::ConvertToOutputPath(makefileIn.c_str());
  std::string dsprule = "${CMAKE_COMMAND}";
  m_Makefile->ExpandVariablesInString(dsprule);
  dsprule = cmSystemTools::ConvertToOutputPath(dsprule.c_str());
  std::string args = makefileIn;
  args += " -H\"";
  args +=
    cmSystemTools::ConvertToOutputPath(m_Makefile->GetHomeDirectory());
  args += "\" -S\"";
  args +=
    cmSystemTools::ConvertToOutputPath(m_Makefile->GetStartDirectory());
  args += "\" -O\"";
  args += 
    cmSystemTools::ConvertToOutputPath(m_Makefile->GetStartOutputDirectory());
  args += "\" -B\"";
  args += 
    cmSystemTools::ConvertToOutputPath(m_Makefile->GetHomeOutputDirectory());
  args += "\"";
  m_Makefile->ExpandVariablesInString(args);

  std::string configFile = 
    m_Makefile->GetDefinition("CMAKE_ROOT");
  configFile += "/Templates/CMakeWindowsSystemConfig.cmake";
  std::vector<std::string> listFiles = m_Makefile->GetListFiles();
  bool found = false;
  for(std::vector<std::string>::iterator i = listFiles.begin();
      i != listFiles.end(); ++i)
    {
    if(*i == configFile)
      {
      found  = true;
      }
    }
  if(!found)
    {
    listFiles.push_back(configFile);
    }
  
  std::vector<std::string> outputs;
  outputs.push_back(dspname);
  cmCustomCommand cc(makefileIn.c_str(), dsprule.c_str(),
                     args.c_str(),
		     listFiles, 
		     outputs);
  sourceGroup.AddCustomCommand(cc);
}


void cmDSPWriter::WriteDSPFile(std::ostream& fout, 
                                 const char *libName,
                                 cmTarget &target)
{
  // We may be modifying the source groups temporarily, so make a copy.
  std::vector<cmSourceGroup> sourceGroups = m_Makefile->GetSourceGroups();
  
  // get the classes from the source lists then add them to the groups
  std::vector<cmSourceFile> classes = target.GetSourceFiles();
  for(std::vector<cmSourceFile>::iterator i = classes.begin(); 
      i != classes.end(); i++)
    {
    // Add the file to the list of sources.
    std::string source = i->GetFullPath();
    cmSourceGroup& sourceGroup = m_Makefile->FindSourceGroup(source.c_str(),
                                                             sourceGroups);
    sourceGroup.AddSource(source.c_str(), &(*i));
    }
  
  // add any custom rules to the source groups
  for (std::vector<cmCustomCommand>::const_iterator cr = 
         target.GetCustomCommands().begin(); 
       cr != target.GetCustomCommands().end(); ++cr)
    {
    cmSourceGroup& sourceGroup = 
      m_Makefile->FindSourceGroup(cr->GetSourceName().c_str(),
                                  sourceGroups);
    cmCustomCommand cc(*cr);
    cc.ExpandVariables(*m_Makefile);
    sourceGroup.AddCustomCommand(cc);
    }
  
  // Write the DSP file's header.
  this->WriteDSPHeader(fout, libName, target, sourceGroups);
  
  // Find the group in which the CMakeLists.txt source belongs, and add
  // the rule to generate this DSP file.
  for(std::vector<cmSourceGroup>::reverse_iterator sg = sourceGroups.rbegin();
      sg != sourceGroups.rend(); ++sg)
    {
    if(sg->Matches("CMakeLists.txt"))
      {
      this->AddDSPBuildRule(*sg);
      break;
      }    
    }
  
  // Loop through every source group.
  for(std::vector<cmSourceGroup>::const_iterator sg = sourceGroups.begin();
      sg != sourceGroups.end(); ++sg)
    {
    const cmSourceGroup::BuildRules& buildRules = sg->GetBuildRules();
    // If the group is empty, don't write it at all.
    if(buildRules.empty())
      { continue; }
    
    // If the group has a name, write the header.
    std::string name = sg->GetName();
    if(name != "")
      {
      this->WriteDSPBeginGroup(fout, name.c_str(), "");
      }
    
    // Loop through each build rule in the source group.
    for(cmSourceGroup::BuildRules::const_iterator cc =
          buildRules.begin(); cc != buildRules.end(); ++ cc)
      {
      std::string source = cc->first;
      const cmSourceGroup::Commands& commands = cc->second.m_Commands;
      const char* compileFlags = 0;
      if(cc->second.m_SourceFile)
        {
        compileFlags = cc->second.m_SourceFile->GetCompileFlags();
        }
      if (source != libName || target.GetType() == cmTarget::UTILITY)
        {
        fout << "# Begin Source File\n\n";
        
        // Tell MS-Dev what the source is.  If the compiler knows how to
        // build it, then it will.
        fout << "SOURCE=" << 
          cmSystemTools::ConvertToOutputPath(source.c_str()) << "\n\n";
        if (!commands.empty())
          {
          cmSourceGroup::CommandFiles totalCommand;
          std::string totalCommandStr;
          totalCommandStr = this->CombineCommands(commands, totalCommand,
                                                  source.c_str());
          this->WriteCustomRule(fout, source.c_str(), totalCommandStr.c_str(), 
                                totalCommand.m_Depends, 
                                totalCommand.m_Outputs, compileFlags);
          }
        else if(compileFlags)
          {
          for(std::vector<std::string>::iterator i
                = m_Configurations.begin(); i != m_Configurations.end(); ++i)
            { 
            if (i == m_Configurations.begin())
              {
              fout << "!IF  \"$(CFG)\" == " << i->c_str() << std::endl;
              }
            else 
              {
              fout << "!ELSEIF  \"$(CFG)\" == " << i->c_str() << std::endl;
              }
            fout << "\n# ADD CPP " << compileFlags << "\n\n";
            } 
          fout << "!ENDIF\n\n";
          }
        fout << "# End Source File\n";
        }
      }
    
    // If the group has a name, write the footer.
    if(name != "")
      {
      this->WriteDSPEndGroup(fout);
      }
    }  

  // Write the DSP file's footer.
  this->WriteDSPFooter(fout);
}


void cmDSPWriter::WriteCustomRule(std::ostream& fout,
                                  const char* source,
                                  const char* command,
                                  const std::set<std::string>& depends,
                                  const std::set<std::string>& outputs,
                                  const char* flags
                                  )
{
  std::vector<std::string>::iterator i;
  for(i = m_Configurations.begin(); i != m_Configurations.end(); ++i)
    {
    if (i == m_Configurations.begin())
      {
      fout << "!IF  \"$(CFG)\" == " << i->c_str() << std::endl;
      }
    else 
      {
      fout << "!ELSEIF  \"$(CFG)\" == " << i->c_str() << std::endl;
      }
    if(flags)
      {
      fout << "\n# ADD CPP " << flags << "\n\n";
      }
    // Write out the dependencies for the rule.
    fout << "USERDEP__HACK=";
    for(std::set<std::string>::const_iterator d = depends.begin();
	d != depends.end(); ++d)
      {
      fout << "\\\n\t" << 
        cmSystemTools::ConvertToOutputPath(d->c_str());
      }
    fout << "\n";

    fout << "# PROP Ignore_Default_Tool 1\n";
    fout << "# Begin Custom Build\n\n";
    if(outputs.size() == 0)
      {
      fout << source << "_force :  \"$(SOURCE)\" \"$(INTDIR)\" \"$(OUTDIR)\"";
      fout << command << "\n\n";
      }
    
    // Write a rule for every output generated by this command.
    for(std::set<std::string>::const_iterator output = outputs.begin();
        output != outputs.end(); ++output)
      {
      fout << "\"" << output->c_str()
           << "\" :  \"$(SOURCE)\" \"$(INTDIR)\" \"$(OUTDIR)\"";
      fout << command << "\n\n";
      }
    
    fout << "# End Custom Build\n\n";
    }
  
  fout << "!ENDIF\n\n";
}


void cmDSPWriter::WriteDSPBeginGroup(std::ostream& fout, 
					const char* group,
					const char* filter)
{
  fout << "# Begin Group \"" << group << "\"\n"
    "# PROP Default_Filter \"" << filter << "\"\n";
}


void cmDSPWriter::WriteDSPEndGroup(std::ostream& fout)
{
  fout << "# End Group\n";
}




void cmDSPWriter::SetBuildType(BuildType b, const char *libName)
{
  std::string root= m_Makefile->GetDefinition("CMAKE_ROOT");
  const char *def= m_Makefile->GetDefinition( "MSPROJECT_TEMPLATE_DIRECTORY");

  if( def)
    {
    root = def;
    }
  else
    {
    root += "/Templates";
    }
  
  switch(b)
    {
    case STATIC_LIBRARY:
      m_DSPHeaderTemplate = root;
      m_DSPHeaderTemplate += "/staticLibHeader.dsptemplate";
      m_DSPFooterTemplate = root;
      m_DSPFooterTemplate += "/staticLibFooter.dsptemplate";
      break;
    case DLL:
      m_DSPHeaderTemplate =  root;
      m_DSPHeaderTemplate += "/DLLHeader.dsptemplate";
      m_DSPFooterTemplate =  root;
      m_DSPFooterTemplate += "/DLLFooter.dsptemplate";
      break;
    case EXECUTABLE:
      m_DSPHeaderTemplate = root;
      m_DSPHeaderTemplate += "/EXEHeader.dsptemplate";
      m_DSPFooterTemplate = root;
      m_DSPFooterTemplate += "/EXEFooter.dsptemplate";
      break;
    case WIN32_EXECUTABLE:
      m_DSPHeaderTemplate = root;
      m_DSPHeaderTemplate += "/EXEWinHeader.dsptemplate";
      m_DSPFooterTemplate = root;
      m_DSPFooterTemplate += "/EXEFooter.dsptemplate";
      break;
    case UTILITY:
      m_DSPHeaderTemplate = root;
      m_DSPHeaderTemplate += "/UtilityHeader.dsptemplate";
      m_DSPFooterTemplate = root;
      m_DSPFooterTemplate += "/UtilityFooter.dsptemplate";
      break;
    }

  // once the build type is set, determine what configurations are
  // possible
  std::ifstream fin(m_DSPHeaderTemplate.c_str());

  cmRegularExpression reg("# Name ");
  if(!fin)
    {
    cmSystemTools::Error("Error Reading ", m_DSPHeaderTemplate.c_str());
    }

  // reset m_Configurations
  m_Configurations.erase(m_Configurations.begin(), m_Configurations.end());
  // now add all the configurations possible
  char buffer[2048];
  while(fin)
    {
    fin.getline(buffer, 2048);
    std::string line = buffer;
    cmSystemTools::ReplaceString(line, "OUTPUT_LIBNAME",libName);
    if (reg.find(line))
      {
      m_Configurations.push_back(line.substr(reg.end()));
      }
    }
}

std::string
cmDSPWriter::CombineCommands(const cmSourceGroup::Commands &commands,
                             cmSourceGroup::CommandFiles &totalCommand,
                             const char *source)
  
{
  // Loop through every custom command generating code from the
  // current source.
  // build up the depends and outputs and commands 
  std::string totalCommandStr = "";
  std::string temp;
  for(cmSourceGroup::Commands::const_iterator c = commands.begin();
      c != commands.end(); ++c)
    {
    totalCommandStr += "\n\t";
    temp= c->second.m_Command; 
    temp = cmSystemTools::ConvertToOutputPath(temp.c_str());
    totalCommandStr += temp;
    totalCommandStr += " ";
    totalCommandStr += c->second.m_Arguments;
    totalCommand.Merge(c->second);
    }      
  // Create a dummy file with the name of the source if it does
  // not exist
  if(totalCommand.m_Outputs.empty())
    { 
    std::string dummyFile = m_Makefile->GetStartOutputDirectory();
    dummyFile += "/";
    dummyFile += source;
    if(!cmSystemTools::FileExists(dummyFile.c_str()))
      {
      std::ofstream fout(dummyFile.c_str());
      fout << "Dummy file created by cmake as unused source for utility command.\n";
      }
    }
  return totalCommandStr;
}


// look for custom rules on a target and collect them together
std::string 
cmDSPWriter::CreateTargetRules(const cmTarget &target, 
                               const char *libName)
{
  std::string customRuleCode = "";

  if (target.GetType() >= cmTarget::UTILITY)
    {
    return customRuleCode;
    }
  
  // Find the group in which the lix exe custom rules belong
  bool init = false;
  for (std::vector<cmCustomCommand>::const_iterator cr = 
         target.GetCustomCommands().begin(); 
       cr != target.GetCustomCommands().end(); ++cr)
    {
    cmCustomCommand cc(*cr);
    cc.ExpandVariables(*m_Makefile);
    if (cc.GetSourceName() == libName)
      {
      if (!init)
        {
        // header stuff
        customRuleCode = "# Begin Special Build Tool\nPostBuild_Cmds=";
        init = true;
        }
      else
        {
        customRuleCode += "\t";
        }
      customRuleCode += cc.GetCommand() + " " + cc.GetArguments();
      }
    }

  if (init)
    {
    customRuleCode += "\n# End Special Build Tool\n";
    }
  return customRuleCode;
}

void cmDSPWriter::WriteDSPHeader(std::ostream& fout, const char *libName,
                                   const cmTarget &target, 
                                 std::vector<cmSourceGroup> &)
{
  std::set<std::string> pathEmitted;
  
  // determine the link directories
  std::string libOptions;
  std::string libDebugOptions;
  std::string libOptimizedOptions;

  std::string libMultiLineOptions;
  std::string libMultiLineDebugOptions;
  std::string libMultiLineOptimizedOptions;

  // suppoirt override in output directory
  std::string libPath = "";
  if (m_Makefile->GetDefinition("LIBRARY_OUTPUT_PATH"))
    {
    libPath = m_Makefile->GetDefinition("LIBRARY_OUTPUT_PATH");
    }
  std::string exePath = "";
  if (m_Makefile->GetDefinition("EXECUTABLE_OUTPUT_PATH"))
    {
    exePath = m_Makefile->GetDefinition("EXECUTABLE_OUTPUT_PATH");
    }

  if(libPath.size())
    {
    // make sure there is a trailing slash
    if(libPath[libPath.size()-1] != '/')
      {
      libPath += "/";
      }
    std::string lpath = 
      cmSystemTools::ConvertToOutputPath(libPath.c_str());
    if(pathEmitted.insert(lpath).second)
      {
      libOptions += " /LIBPATH:\"";
      libOptions += lpath;
      libOptions += "$(INTDIR)\" ";
      libOptions += " /LIBPATH:\"";
      libOptions += lpath;
      libOptions += "\" ";
      libMultiLineOptions += "# ADD LINK32 /LIBPATH:\"";
      libMultiLineOptions += lpath;
      libMultiLineOptions += "$(INTDIR)\" ";
      libMultiLineOptions += " /LIBPATH:\"";
      libMultiLineOptions += lpath;
      libMultiLineOptions += "\" \n";
      }
    }
  if(exePath.size())
    {
    // make sure there is a trailing slash
    if(exePath[exePath.size()-1] != '/')
      {
      exePath += "/";
      }
    std::string lpath = 
      cmSystemTools::ConvertToOutputPath(exePath.c_str());
    if(pathEmitted.insert(lpath).second)
      {
      libOptions += " /LIBPATH:\"";
      libOptions += lpath;
      libOptions += "$(INTDIR)\" ";
      libOptions += " /LIBPATH:\"";
      libOptions += lpath;
      libOptions += "\" ";
      libMultiLineOptions += "# ADD LINK32 /LIBPATH:\"";
      libMultiLineOptions += lpath;
      libMultiLineOptions += "$(INTDIR)\" ";
      libMultiLineOptions += " /LIBPATH:\"";
      libMultiLineOptions += lpath;
      libMultiLineOptions += "\" \n";
      }
    }
  std::vector<std::string>::iterator i;
  std::vector<std::string>& libdirs = m_Makefile->GetLinkDirectories();
  for(i = libdirs.begin(); i != libdirs.end(); ++i)
    {
    std::string lpath = 
      cmSystemTools::ConvertToOutputPath(i->c_str());
    if(lpath[lpath.size()-1] != '/')
      {
      lpath += "/";
      }
    if(pathEmitted.insert(lpath).second)
      {
      libOptions += " /LIBPATH:\"";
      libOptions += lpath;
      libOptions += "/$(INTDIR)\" ";
      libOptions += " /LIBPATH:\"";
      libOptions += lpath;
      libOptions += "\" ";
      
      libMultiLineOptions += "# ADD LINK32 /LIBPATH:\"";
      libMultiLineOptions += lpath;
      libMultiLineOptions += "/$(INTDIR)\" ";
      libMultiLineOptions += " /LIBPATH:\"";
      libMultiLineOptions += lpath;
      libMultiLineOptions += "\" \n";
      }
    }
  
  // find link libraries
  const cmTarget::LinkLibraries& libs = target.GetLinkLibraries();
  cmTarget::LinkLibraries::const_iterator j;
  for(j = libs.begin(); j != libs.end(); ++j)
    {
    // add libraries to executables and dlls (but never include
    // a library in a library, bad recursion)
    if ((target.GetType() != cmTarget::SHARED_LIBRARY
         && target.GetType() != cmTarget::STATIC_LIBRARY) || 
        (target.GetType() == cmTarget::SHARED_LIBRARY && libName != j->first))
      {
      std::string lib = j->first;
      if(j->first.find(".lib") == std::string::npos)
        {
        lib += ".lib";
        }
      lib = cmSystemTools::ConvertToOutputPath(lib.c_str());

      if (j->second == cmTarget::GENERAL)
        {
        libOptions += " ";
        libOptions += lib;
        
        libMultiLineOptions += "# ADD LINK32 ";
        libMultiLineOptions +=  lib;
        libMultiLineOptions += "\n";
        }
      if (j->second == cmTarget::DEBUG)
        {
        libDebugOptions += " ";
        libDebugOptions += lib;

        libMultiLineDebugOptions += "# ADD LINK32 ";
        libMultiLineDebugOptions += lib;
        libMultiLineDebugOptions += "\n";
        }
      if (j->second == cmTarget::OPTIMIZED)
        {
        libOptimizedOptions += " ";
        libOptimizedOptions += lib;

        libMultiLineOptimizedOptions += "# ADD LINK32 ";
        libMultiLineOptimizedOptions += lib;
        libMultiLineOptimizedOptions += "\n";
        }      
      }
    }
  std::string extraLinkOptions = 
    m_Makefile->GetDefinition("CMAKE_EXTRA_LINK_FLAGS");
  if(extraLinkOptions.size())
    {
    libOptions += " ";
    libOptions += extraLinkOptions;
    libOptions += " ";
    libMultiLineOptions += "# ADD LINK32 ";
    libMultiLineOptions +=  extraLinkOptions;
    libMultiLineOptions += " \n";
    }
  
  // are there any custom rules on the target itself
  // only if the target is a lib or exe
  std::string customRuleCode = this->CreateTargetRules(target, libName);

  std::ifstream fin(m_DSPHeaderTemplate.c_str());
  if(!fin)
    {
    cmSystemTools::Error("Error Reading ", m_DSPHeaderTemplate.c_str());
    }
  char buffer[2048];

  while(fin)
    {
      fin.getline(buffer, 2048);
      std::string line = buffer;
      const char* mfcFlag = m_Makefile->GetDefinition("CMAKE_MFC_FLAG");
      if(!mfcFlag)
        {
        mfcFlag = "0";
        }
      cmSystemTools::ReplaceString(line, "CMAKE_CUSTOM_RULE_CODE",
                                   customRuleCode.c_str());
      cmSystemTools::ReplaceString(line, "CMAKE_MFC_FLAG",
                                   mfcFlag);
      cmSystemTools::ReplaceString(line, "CM_LIBRARIES",
                                   libOptions.c_str());
      cmSystemTools::ReplaceString(line, "CM_DEBUG_LIBRARIES",
                                   libDebugOptions.c_str());
      cmSystemTools::ReplaceString(line, "CM_OPTIMIZED_LIBRARIES",
                                   libOptimizedOptions.c_str());

      cmSystemTools::ReplaceString(line, "CM_MULTILINE_LIBRARIES",
                                   libMultiLineOptions.c_str());
      cmSystemTools::ReplaceString(line, "CM_MULTILINE_DEBUG_LIBRARIES",
                                   libMultiLineDebugOptions.c_str());
      cmSystemTools::ReplaceString(line, "CM_MULTILINE_OPTIMIZED_LIBRARIES",
                                   libMultiLineOptimizedOptions.c_str());

      cmSystemTools::ReplaceString(line, "BUILD_INCLUDES",
                                   m_IncludeOptions.c_str());
      cmSystemTools::ReplaceString(line, "OUTPUT_LIBNAME",libName);
      cmSystemTools::ReplaceString(line, "LIBRARY_OUTPUT_PATH",
                                   cmSystemTools::ConvertToOutputPath(libPath.c_str()).c_str());
      cmSystemTools::ReplaceString(line, "EXECUTABLE_OUTPUT_PATH",
                                   cmSystemTools::ConvertToOutputPath(exePath.c_str()).c_str());
      cmSystemTools::ReplaceString(line, 
                                   "EXTRA_DEFINES", 
				   m_Makefile->GetDefineFlags());
      cmSystemTools::ReplaceString(line,
                                   "CMAKE_CXX_FLAGS_RELEASE",
                                   m_Makefile->
                                   GetDefinition("CMAKE_CXX_FLAGS_RELEASE"));
      cmSystemTools::ReplaceString(line,
                                   "CMAKE_CXX_FLAGS_MINSIZEREL",
                                   m_Makefile->
                                   GetDefinition("CMAKE_CXX_FLAGS_MINSIZEREL")
        );
      cmSystemTools::ReplaceString(line,
                                   "CMAKE_CXX_FLAGS_DEBUG",
                                   m_Makefile->
                                   GetDefinition("CMAKE_CXX_FLAGS_DEBUG"));
      cmSystemTools::ReplaceString(line,
                                   "CMAKE_CXX_FLAGS_RELWITHDEBINFO",
                                   m_Makefile->
                                   GetDefinition("CMAKE_CXX_FLAGS_RELWITHDEBINFO"));
      cmSystemTools::ReplaceString(line,
                                   "CMAKE_CXX_FLAGS",
                                   m_Makefile->
                                   GetDefinition("CMAKE_CXX_FLAGS"));
      
      fout << line.c_str() << std::endl;
    }
}


void cmDSPWriter::WriteDSPFooter(std::ostream& fout)
{  
  std::ifstream fin(m_DSPFooterTemplate.c_str());
  if(!fin)
    {
    cmSystemTools::Error("Error Reading ",
                         m_DSPFooterTemplate.c_str());
    }
  char buffer[2048];
  while(fin)
    {
      fin.getline(buffer, 2048);
      fout << buffer << std::endl;
    }
}
