
/*============================================================================
	CMake - Cross Platform Makefile Generator
	Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

	Distributed under the OSI-approved BSD License (the "License");
	see accompanying file Copyright.txt for details.

	This software is distributed WITHOUT ANY WARRANTY; without even the
	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the License for more information.
============================================================================*/
/*============================================================================
	Development progress:

	Tasks/Issues:
	 - Execute unit tests against the generator somehow
	 - Fix target aliases being repeated in the output
	 - Fix cmake build using fastbuild (currently appears configuration incorrect)
	 - Running some of the Cmake generation, the pdb files can't be deleted (shows up errors)
	 - Depends upon visual studio generator code to sort dependencies
	 - When generating CMAKE from scratch, it sometimes errors with fortran complaints and fails generation?
	 a re-run will succeed.
	 - Linker for msvc uses the cmake command to callback. Don't think this is an issue since
	 I think compilation is the part that gets distributed anyway.
	 But it might mean that the cache has trouble calculating deps for obj->lib/exe.
	 Not sure if Fastbuild supports that anyway yet
	 - Need to sort custom build commands by their outputs

	Fastbuild bugs:
	 - Defining prebuild dependencies that don't exist, causes the error output when that
	 target is actually defined. Rather than originally complaining that the target
	 doesn't exist where the reference is attempted.
	 - Parsing strings with double $$ doesn't generate a nice error
	 - Undocumented that you can escape a $ with ^$
	 - ExecInputs is invalid empty
	 - Would be great if you could define dummy targets (maybe blank aliases?)
	 - Exec nodes need to not worry about dummy output files not being created
	 - Would be nice if nodes didn't need to be completely in order. But then cycles would be possible
	 - Implib directory is not created for exeNodes (DLLs work now though)

	Limitations:
	 - Only tested/working with MSVC

	Notes:
	 - Understanding Custom Build Steps and Build Events
		 https://msdn.microsoft.com/en-us/library/e85wte0k.aspx
	 very useful documentation detailing the order of execution
	 of standard MSVC events. This is useful to determine correct
	 behaviour of fastbuild generator (view a project generated to MSVC,
	 then apply the same rules/assumptions back into fastbuild).
	 i.e. Custom rules are always executed first.

	Current list of unit tests failing:

	91% tests passed, 32 tests failed out of 371
	Total Test time (real) = 1479.86 sec

The following tests FAILED:
	 59 - Preprocess (Failed)
	 60 - ExportImport (Failed)
	 68 - StagingPrefix (Failed)
	 70 - ConfigSources (Failed)
	 79 - Module.GenerateExportHeader (Failed)
	101 - SubProject-Stage2 (Failed)
	113 - BuildDepends (Failed)
	114 - SimpleInstall (Failed)
	115 - SimpleInstall-Stage2 (Failed)
	127 - complex (Failed)
	128 - complexOneConfig (Failed)
	131 - ExternalProject (Failed)
	132 - ExternalProjectLocal (Failed)
	133 - ExternalProjectUpdateSetup (Failed)
	134 - ExternalProjectUpdate (Failed)
	151 - Plugin (Failed)
	157 - PrecompiledHeader (Failed)
	184 - CTestConfig.Script.Debug (Failed)
	185 - CTestConfig.Dashboard.Debug (Failed)
	186 - CTestConfig.Script.MinSizeRel (Failed)
	187 - CTestConfig.Dashboard.MinSizeRel (Failed)
	188 - CTestConfig.Script.Release (Failed)
	189 - CTestConfig.Dashboard.Release (Failed)
	190 - CTestConfig.Script.RelWithDebInfo (Failed)
	191 - CTestConfig.Dashboard.RelWithDebInfo (Failed)
	197 - CMakeCommands.target_compile_options (Failed)
	238 - CMakeOnly.CheckStructHasMember (Failed)
	275 - RunCMake.Configure (Failed)
	331 - RunCMake.File_Generate (Failed)
	371 - CMake.CheckSourceTree (Failed)
============================================================================*/
#include "cmGlobalFastbuildGenerator.h"

#include "cmGeneratorTarget.h"
#include "cmGlobalGeneratorFactory.h"
#include "cmLocalFastbuildGenerator.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmTarget.h"
#include "cmGeneratedFileStream.h"
#include "cmLocalGenerator.h"
#include "cmComputeLinkInformation.h"
#include "cmGlobalVisualStudioGenerator.h"
#include "cmCustomCommandGenerator.h"
#include <cmsys/Encoding.hxx>
#include <assert.h>

static const char fastbuildGeneratorName[] = "Fastbuild";

class cmGlobalFastbuildGenerator::Factory 
	: public cmGlobalGeneratorFactory
{
public:
	Factory()
	{

	}

	cmGlobalGenerator* CreateGlobalGenerator(const std::string& name) const 
	{
		if (name == (fastbuildGeneratorName))
		{
			return new cmGlobalFastbuildGenerator();
		}
		return NULL;
	}

	void GetDocumentation(cmDocumentationEntry& entry) const 
	{
		entry.Name = fastbuildGeneratorName;
		entry.Brief = "Generates fastbuild project files.";
	}

	void GetGenerators(std::vector<std::string>& names) const 
	{
		names.push_back(fastbuildGeneratorName);
	}
};

//----------------------------------------------------------------------------
class cmGlobalFastbuildGenerator::Detail
{
public:
	class FileContext;
	class Definition;
	class Detection;
	class Generation;
};

//----------------------------------------------------------------------------
class cmGlobalFastbuildGenerator::Detail::FileContext
{
public:
	FileContext(cmGeneratedFileStream & afout)
		: fout(afout)
	{
	}

	void WriteComment(const std::string& comment)
	{
		fout << linePrefix << ";" << comment << "\n";
	}

	void WriteBlankLine()
	{
		fout << "\n";
	}

	void WriteHorizontalLine()
	{
		fout <<
			";-------------------------------------------------------------------------------\n";
	}

	void WriteSectionHeader(const char * section)
	{
		fout << "\n";
		WriteHorizontalLine();
		WriteComment(section);
		WriteHorizontalLine();
	}

	void WritePushScope(char begin = '{', char end = '}')
	{
		fout << linePrefix << begin << "\n";
		linePrefix += "\t";
		closingScope += end;
	}

	void WritePushScopeStruct()
	{
		WritePushScope('[', ']');
	}

	void WritePopScope()
	{
		assert(!linePrefix.empty());
		linePrefix.resize(linePrefix.size() - 1);

		fout << linePrefix << closingScope[closingScope.size() - 1] <<
			"\n";

		closingScope.resize(closingScope.size() - 1);
	}

	void WriteVariable(const std::string& key, const std::string& value,
		const std::string& operation = "=")
	{
		fout << linePrefix << "." <<
			key << " " << operation << " " << value << "\n";
	}

	void WriteCommand(const std::string& command, const std::string& value = std::string())
	{
		fout << linePrefix << command;
		if (!value.empty())
		{
			fout << "(" << value << ")";
		}
		fout << "\n";
	}

	void WriteArray(const std::string& key,
		const std::vector<std::string>& values,
		const std::string& operation = "=")
	{
		WriteVariable(key, "", operation);
		WritePushScope();
		size_t size = values.size();
		for (size_t index = 0; index < size; ++index)
		{
			const std::string & value = values[index];
			bool isLast = index == size - 1;

			fout << linePrefix << value;
			if (!isLast)
			{
				fout << ',';
			}
			fout << "\n";
		}
		WritePopScope();
	}

private:
	cmGeneratedFileStream & fout;
	std::string linePrefix;
	std::string closingScope;
};

//----------------------------------------------------------------------------
class cmGlobalFastbuildGenerator::Detail::Detection
{
public:
	static std::string GetLastFolderName(const std::string& string)
	{
		return string.substr(string.rfind('/'));
	}

	static bool IsExcludedFromAll( cmTarget* target )
	{
		bool excluded = target->GetPropertyAsBool("EXCLUDE_FROM_ALL");
		cmLocalGenerator* lg = target->GetMakefile()->GetLocalGenerator();
		while(lg->GetParent() != 0 && !excluded)
		{
			excluded = lg->GetMakefile()->GetPropertyAsBool("EXCLUDE_FROM_ALL");
			lg = lg->GetParent();
		}

		return excluded;
	}

#define FASTBUILD_DOLLAR_TAG "FASTBUILD_DOLLAR_TAG"

	static void UnescapeFastbuildVariables(std::string& string)
	{
		// Unescape the Fastbuild configName symbol with $
		cmSystemTools::ReplaceString(string, "^", "^^");
		cmSystemTools::ReplaceString(string, "$$", "^$");
 		cmSystemTools::ReplaceString(string, FASTBUILD_DOLLAR_TAG, "$");
		//cmSystemTools::ReplaceString(string, "$$ConfigName$$", "$ConfigName$");
		//cmSystemTools::ReplaceString(string, "^$ConfigName^$", "$ConfigName$");
	}

	static void ResolveFastbuildVariables(std::string& string, const std::string& configName)
	{
		// Replace Fastbuild configName with the config name
		cmSystemTools::ReplaceString(string, "$ConfigName$", configName);
	}

	static std::string BuildCommandLine(
		const std::vector<std::string> &cmdLines)
	{
#ifdef _WIN32
		const char * cmdExe = "cmd.exe";
		std::string cmdExeAbsolutePath = cmSystemTools::FindProgram(cmdExe);
#endif

		// If we have no commands but we need to build a command anyway, use ":".
		// This happens when building a POST_BUILD value for link targets that
		// don't use POST_BUILD.
		if (cmdLines.empty())
		{
#ifdef _WIN32
			return cmdExeAbsolutePath + " /C \"cd .\"";
#else
			return ":";
#endif
		}

		std::ostringstream cmd;
		for (std::vector<std::string>::const_iterator li = cmdLines.begin();
			li != cmdLines.end(); ++li)
#ifdef _WIN32
		{
			if (li != cmdLines.begin())
			{
				cmd << " && ";
			}
			else if (cmdLines.size() > 1)
			{
				cmd << cmdExeAbsolutePath << " /C \"";
			}
			cmd << *li;
		}
		if (cmdLines.size() > 1)
		{
			cmd << "\"";
		}
#else
		{
			if (li != cmdLines.begin())
			{
				cmd << " && ";
			}
			cmd << *li;
		}
#endif
		std::string cmdOut = cmd.str();
		UnescapeFastbuildVariables(cmdOut);

		return cmdOut;
	}

	static void DetectConfigurations(cmGlobalFastbuildGenerator * self,
		cmMakefile* mf,
		std::vector<std::string> & configurations)
	{
		// process the configurations
		const char* configList =
			self->GetCMakeInstance()->GetCacheDefinition("CMAKE_CONFIGURATION_TYPES");
		if (configList)
		{
			std::vector<std::string> argsOut;
			cmSystemTools::ExpandListArgument(configList, argsOut);
			for (std::vector<std::string>::iterator iter = argsOut.begin();
				iter != argsOut.end(); ++iter)
			{
				if (std::find(configurations.begin(), configurations.end(), *iter) == configurations.end())
				{
					configurations.push_back(*iter);
				}
			}
		}

		// default to at least Debug and Release
		if (configurations.size() == 0)
		{
			configurations.push_back("Debug");
			configurations.push_back("Release");
			configurations.push_back("MinSizeRel");
			configurations.push_back("RelWithDebInfo");
		}

		// Reset the entry to have a semi-colon separated list.
		std::string configs = configurations[0];
		for (unsigned int i = 1; i < configurations.size(); ++i)
		{
			configs += ";";
			configs += configurations[i];
		}

		// Add a cache definition
		mf->AddCacheDefinition(
			"CMAKE_CONFIGURATION_TYPES",
			configs.c_str(),
			"Semicolon separated list of supported configuration types, "
			"only supports Debug, Release, MinSizeRel, and RelWithDebInfo, "
			"anything else will be ignored.",
			cmCacheManager::STRING);
	}

	struct FastbuildTargetNames
	{
		std::string targetNameOut;
		std::string targetNameReal;
		std::string targetNameImport;
		std::string targetNamePDB;
		std::string targetNameSO;
	
		std::string targetOutput;
		std::string targetOutputReal;
		std::string targetOutputImplib;
		std::string targetOutputDir;
		std::string targetOutputPDBDir;
		std::string targetOutputCompilePDBDir;
	};

	static void DetectOutput(
		FastbuildTargetNames & targetNamesOut,
		cmTarget &target,
		const std::string & configName)
	{
		if (target.GetType() == cmTarget::EXECUTABLE)
		{
			target.GetExecutableNames(
				targetNamesOut.targetNameOut,
				targetNamesOut.targetNameReal,
				targetNamesOut.targetNameImport,
				targetNamesOut.targetNamePDB,
				configName);
		}
		else
		{
			target.GetLibraryNames(
				targetNamesOut.targetNameOut,
				targetNamesOut.targetNameSO,
				targetNamesOut.targetNameReal,
				targetNamesOut.targetNameImport,
				targetNamesOut.targetNamePDB,
				configName);
		}
		
		if (target.HaveWellDefinedOutputFiles())
		{
			targetNamesOut.targetOutputDir = target.GetDirectory(configName) + "/";

			targetNamesOut.targetOutput = target.GetFullPath(configName);
			targetNamesOut.targetOutputReal = target.GetFullPath(configName,
				/*implib=*/false,
				/*realpath=*/true);
			targetNamesOut.targetOutputImplib = target.GetFullPath(configName,
				/*implib=*/true);
		}
		else
		{
			targetNamesOut.targetOutputDir = target.GetMakefile()->GetStartOutputDirectory();
			if (targetNamesOut.targetOutputDir.empty() || 
				targetNamesOut.targetOutputDir == ".")
			{
				targetNamesOut.targetOutputDir = target.GetName();
			}
			else 
			{
				targetNamesOut.targetOutputDir += "/";
				targetNamesOut.targetOutputDir += target.GetName();
			}
			targetNamesOut.targetOutputDir += "/";
			targetNamesOut.targetOutputDir += configName;
			targetNamesOut.targetOutputDir += "/";

			targetNamesOut.targetOutput = targetNamesOut.targetOutputDir + "/" +
				targetNamesOut.targetNameOut;
			targetNamesOut.targetOutputImplib = targetNamesOut.targetOutputDir + "/" +
				targetNamesOut.targetNameImport;
			targetNamesOut.targetOutputReal = targetNamesOut.targetOutputDir + "/" +
				targetNamesOut.targetNameReal;
		}

		if (target.GetType() == cmTarget::EXECUTABLE ||
			target.GetType() == cmTarget::STATIC_LIBRARY ||
			target.GetType() == cmTarget::SHARED_LIBRARY ||
			target.GetType() == cmTarget::MODULE_LIBRARY)
		{
			targetNamesOut.targetOutputPDBDir = target.GetPDBDirectory(configName);
			targetNamesOut.targetOutputPDBDir += "/";
		}
		if (target.GetType() <= cmTarget::OBJECT_LIBRARY)
		{
			targetNamesOut.targetOutputCompilePDBDir = target.GetCompilePDBPath(configName);
			if (targetNamesOut.targetOutputCompilePDBDir.empty())
			{
				targetNamesOut.targetOutputCompilePDBDir = target.GetSupportDirectory() + "/";
			}
		}

		// Make sure all obey the correct slashes
		cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutput);
		cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputImplib);
		cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputReal);
		cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputDir);
		cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputPDBDir);
		cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputCompilePDBDir);
	}

	static void ComputeLinkCmds(std::vector<std::string> & linkCmds,
		cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		cmGeneratorTarget *gt,
		std::string configName)
	{
		const std::string& linkLanguage = target.GetLinkerLanguage(configName);
		cmMakefile* mf = lg->GetMakefile();
		{
			std::string linkCmdVar = 
				gt->GetCreateRuleVariable(linkLanguage, configName);
			const char *linkCmd = mf->GetDefinition(linkCmdVar);
			if (linkCmd)
			{
				cmSystemTools::ExpandListArgument(linkCmd, linkCmds);
				return;
			}
		}

		// If the above failed, then lets try this:
		switch (target.GetType()) 
		{
			case cmTarget::STATIC_LIBRARY: 
			{
				// We have archive link commands set. First, delete the existing archive.
				{
					std::string cmakeCommand = lg->ConvertToOutputFormat(
						mf->GetRequiredDefinition("CMAKE_COMMAND"),
						cmLocalGenerator::SHELL);
					linkCmds.push_back(cmakeCommand + " -E remove $TARGET_FILE");
				}
				// TODO: Use ARCHIVE_APPEND for archives over a certain size.
				{
					std::string linkCmdVar = "CMAKE_";
					linkCmdVar += linkLanguage;
					linkCmdVar += "_ARCHIVE_CREATE";
					const char *linkCmd = mf->GetRequiredDefinition(linkCmdVar);
					cmSystemTools::ExpandListArgument(linkCmd, linkCmds);
				}
				{
					std::string linkCmdVar = "CMAKE_";
					linkCmdVar += linkLanguage;
					linkCmdVar += "_ARCHIVE_FINISH";
					const char *linkCmd = mf->GetRequiredDefinition(linkCmdVar);
					cmSystemTools::ExpandListArgument(linkCmd, linkCmds);
				}
				return;
			}
			case cmTarget::SHARED_LIBRARY:
			case cmTarget::MODULE_LIBRARY:
			case cmTarget::EXECUTABLE:
				break;
			default:
				assert(0 && "Unexpected target type");
		}
		return;
	}

	static std::string ComputeDefines(
		cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		const cmSourceFile* source,
		const std::string& configName,
		const std::string& language)
	{
		std::set<std::string> defines;

		// Add the export symbol definition for shared library objects.
		if(const char* exportMacro = target.GetExportMacro())
		{
			lg->AppendDefines(defines, exportMacro);
		}

		// Add preprocessor definitions for this target and configuration.
		lg->AddCompileDefinitions(defines, &target,
			configName, language);

		if (source)
		{
			lg->AppendDefines(defines,
				source->GetProperty("COMPILE_DEFINITIONS"));

			std::string defPropName = "COMPILE_DEFINITIONS_";
			defPropName += cmSystemTools::UpperCase(configName);
			lg->AppendDefines(defines,
				source->GetProperty(defPropName));
		}

		// Add a definition for the configuration name.
		// NOTE: CMAKE_TEST_REQUIREMENT The following was added specifically to 
		// facillitate cmake testing. Doesn't feel right to do this...
		std::string configDefine = "CMAKE_INTDIR=\"";
		configDefine += configName;
		configDefine += "\"";
		lg->AppendDefines(defines, configDefine);

		std::string definesString;
		lg->JoinDefines(defines, definesString,
			language);

		return definesString;
	}

	static void DetectLinkerLibPaths(
		std::string& linkerLibPath,
		cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		const std::string & configName )
	{
		cmMakefile* pMakefile = lg->GetMakefile();
		cmComputeLinkInformation* pcli = target.GetLinkInformation(configName);
		if (!pcli)
		{
			// No link information, then no linker library paths
			return;
		}
		cmComputeLinkInformation& cli = *pcli;

		std::string libPathFlag =
			pMakefile->GetRequiredDefinition("CMAKE_LIBRARY_PATH_FLAG");
		std::string libPathTerminator =
			pMakefile->GetSafeDefinition("CMAKE_LIBRARY_PATH_TERMINATOR");

		// Append the library search path flags.
		std::vector<std::string> const& libDirs = cli.GetDirectories();
		for (std::vector<std::string>::const_iterator libDir = libDirs.begin();
			libDir != libDirs.end(); ++libDir)
		{
			std::string libpath = lg->ConvertToOutputForExisting(*libDir,
				cmLocalGenerator::START_OUTPUT,
				cmLocalGenerator::SHELL);
			cmSystemTools::ConvertToOutputSlashes(libpath);

			// Add the linker lib path twice, once raw, then once with
			// the configname attached
			std::string configlibpath = libpath + "/" + configName;
			cmSystemTools::ConvertToOutputSlashes(configlibpath);

			linkerLibPath += " " + libPathFlag;
			linkerLibPath += libpath;
			linkerLibPath += libPathTerminator;

			linkerLibPath += " " + libPathFlag;
			linkerLibPath += configlibpath;
			linkerLibPath += libPathTerminator;
			linkerLibPath += " ";
		}
	}

	static bool DetectBaseLinkerCommand(std::string & command,
		cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		cmGeneratorTarget *gt,
		const std::string & configName)
	{
		const std::string& linkLanguage = target.GetLinkerLanguage(configName);
		if (linkLanguage.empty()) {
			cmSystemTools::Error("CMake can not determine linker language for "
				"target: ",
				target.GetName().c_str());
			return false;
		}

		cmLocalGenerator::RuleVariables vars;
		vars.RuleLauncher = "RULE_LAUNCH_LINK";
		vars.CMTarget = &target;
		vars.Language = linkLanguage.c_str();

		std::string responseFlag;
		vars.Objects = FASTBUILD_DOLLAR_TAG "FB_INPUT_1_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
		vars.LinkLibraries = "";
		
		vars.ObjectDir = FASTBUILD_DOLLAR_TAG "TargetOutDir" FASTBUILD_DOLLAR_TAG;
		vars.Target = FASTBUILD_DOLLAR_TAG "FB_INPUT_2_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;

		vars.TargetSOName = FASTBUILD_DOLLAR_TAG"TargetOutSO" FASTBUILD_DOLLAR_TAG;
		vars.TargetPDB = FASTBUILD_DOLLAR_TAG "TargetOutPDBDir" FASTBUILD_DOLLAR_TAG FASTBUILD_DOLLAR_TAG "TargetNamePDB" FASTBUILD_DOLLAR_TAG;

		// Setup the target version.
		std::string targetVersionMajor;
		std::string targetVersionMinor;
		{
			std::ostringstream majorStream;
			std::ostringstream minorStream;
			int major;
			int minor;
			target.GetTargetVersion(major, minor);
			majorStream << major;
			minorStream << minor;
			targetVersionMajor = majorStream.str();
			targetVersionMinor = minorStream.str();
		}
		vars.TargetVersionMajor = targetVersionMajor.c_str();
		vars.TargetVersionMinor = targetVersionMinor.c_str();

		vars.Defines = FASTBUILD_DOLLAR_TAG "CompileDefineFlags" FASTBUILD_DOLLAR_TAG;
		vars.Flags = FASTBUILD_DOLLAR_TAG "TargetFlags" FASTBUILD_DOLLAR_TAG;
		vars.LinkFlags = FASTBUILD_DOLLAR_TAG "LinkFlags" FASTBUILD_DOLLAR_TAG " " FASTBUILD_DOLLAR_TAG "LinkPath" FASTBUILD_DOLLAR_TAG;
		// Rule for linking library/executable.
		std::vector<std::string> linkCmds;
		ComputeLinkCmds(linkCmds, lg, target, gt, configName);
		for (std::vector<std::string>::iterator i = linkCmds.begin();
			i != linkCmds.end();
			++i)
		{
			lg->ExpandRuleVariables(*i, vars);
		}
		
		command = BuildCommandLine(linkCmds);

		return true;
	}

	static void SplitExecutableAndFlags(const std::string & command,
		std::string & executable, std::string & options)
	{
		// Remove the command from the front
		std::vector<std::string> args = cmSystemTools::ParseArguments(command.c_str());

		// Join the args together and remove 0 from the front
		std::stringstream argSet;
		std::copy(args.begin() + 1, args.end(), std::ostream_iterator<std::string>(argSet, " "));
		
		executable = args[0];
		options = argSet.str();
	}

	static void DetectBaseCompileCommand(std::string & command,
		cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		const std::string & language)
	{
		cmLocalGenerator::RuleVariables compileObjectVars;
		compileObjectVars.CMTarget = &target;
		compileObjectVars.Language = language.c_str();
		compileObjectVars.Source = FASTBUILD_DOLLAR_TAG "FB_INPUT_1_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
		compileObjectVars.Object = FASTBUILD_DOLLAR_TAG "FB_INPUT_2_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
		compileObjectVars.ObjectDir = FASTBUILD_DOLLAR_TAG "TargetOutputDir" FASTBUILD_DOLLAR_TAG;
		compileObjectVars.ObjectFileDir = "";
		compileObjectVars.Flags = "";
		compileObjectVars.Defines = "";
		compileObjectVars.TargetCompilePDB = FASTBUILD_DOLLAR_TAG "TargetOutCompilePDBDir" FASTBUILD_DOLLAR_TAG FASTBUILD_DOLLAR_TAG "TargetNamePDB" FASTBUILD_DOLLAR_TAG;

		// Rule for compiling object file.
		std::string compileCmdVar = "CMAKE_";
		compileCmdVar += language;
		compileCmdVar += "_COMPILE_OBJECT";
		std::string compileCmd = lg->GetMakefile()->GetRequiredDefinition(compileCmdVar);
		std::vector<std::string> compileCmds;
		cmSystemTools::ExpandListArgument(compileCmd, compileCmds);

		for (std::vector<std::string>::iterator i = compileCmds.begin();
			i != compileCmds.end(); ++i)
		{
			std::string & compileCmdStr = *i;
			lg->ExpandRuleVariables(compileCmdStr, compileObjectVars);
		}

		command = BuildCommandLine(compileCmds);
	}

	static std::string DetectCompileRule(cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		const std::string& lang)
	{
		cmLocalGenerator::RuleVariables vars;
		vars.RuleLauncher = "RULE_LAUNCH_COMPILE";
		vars.CMTarget = &target;
		vars.Language = lang.c_str();
		vars.Source = "$in";
		vars.Object = "$out";
		vars.Defines = "$DEFINES";
		vars.TargetPDB = "$TARGET_PDB";
		vars.TargetCompilePDB = "$TARGET_COMPILE_PDB";
		vars.ObjectDir = "$OBJECT_DIR";
		vars.ObjectFileDir = "$OBJECT_FILE_DIR";
		vars.Flags = "$FLAGS";

		cmMakefile* mf = lg->GetMakefile();

		// Rule for compiling object file.
		const std::string cmdVar = std::string("CMAKE_") + lang + "_COMPILE_OBJECT";
		std::string compileCmd = mf->GetRequiredDefinition(cmdVar);
		std::vector<std::string> compileCmds;
		cmSystemTools::ExpandListArgument(compileCmd, compileCmds);

		for (std::vector<std::string>::iterator i = compileCmds.begin();
			i != compileCmds.end(); ++i)
		{
			std::string & compileCmdStr = *i;
			lg->ExpandRuleVariables(compileCmdStr, vars);
		}

		std::string cmdLine =
			BuildCommandLine(compileCmds);

		return cmdLine;
	}

	static void DetectLanguages(std::set<std::string> & languages,
		cmGlobalFastbuildGenerator * self,
		cmTarget &target)
	{
		// Object libraries do not have linker stages
		// nor utilities
		bool hasObjectGroups =
			target.GetType() != cmTarget::UTILITY &&
			target.GetType() != cmTarget::GLOBAL_TARGET;
		if (!hasObjectGroups)
		{
			return;
		}

		cmGeneratorTarget *gt = self->GetGeneratorTarget(&target);

		std::vector<std::string>::const_iterator
			iter = self->GetConfigurations().begin(),
			end = self->GetConfigurations().end();
		for (; iter != end; ++iter)
		{
			const std::string & configName = *iter;

			std::vector<const cmSourceFile*> sourceFiles;
			gt->GetObjectSources(sourceFiles, configName);
			for (std::vector<const cmSourceFile*>::const_iterator
				i = sourceFiles.begin(); i != sourceFiles.end(); ++i)
			{
				const std::string& lang = (*i)->GetLanguage();
				if (!lang.empty())
				{
					languages.insert(lang);
				}
			}
		}
	}

	static void FilterSourceFiles(std::vector<cmSourceFile const*> & filteredSourceFiles,
		std::vector<cmSourceFile const*> & sourceFiles, const std::string & language)
	{
		for (std::vector<cmSourceFile const*>::const_iterator
			i = sourceFiles.begin(); i != sourceFiles.end(); ++i)
		{
			const cmSourceFile* sf = *i;
			if (sf->GetLanguage() == language)
			{
				filteredSourceFiles.push_back(sf);
			}
		}
	}

	static void DetectCompilerFlags(std::string & compileFlags,
		cmLocalFastbuildGenerator *lg,
		cmGeneratorTarget *gt,
		cmTarget &target,
		const cmSourceFile* source,
		const std::string& language,
		const std::string& configName)
	{
		lg->AddLanguageFlags(compileFlags, 
			language, 
			configName);

		lg->AddArchitectureFlags(compileFlags,
			gt,
			language,
			configName);

		// Add shared-library flags if needed.
		lg->AddCMP0018Flags(compileFlags, 
			&target,
			language,
			configName);

		lg->AddVisibilityPresetFlags(compileFlags, &target,
			language);

		std::vector<std::string> includes;
		lg->GetIncludeDirectories(includes,
			gt,
			language,
			configName);

		// Add include directory flags.
		std::string includeFlags = lg->GetIncludeFlags(includes, gt,
			language,
			language == "RC" ? true : false,  // full include paths for RC
			// needed by cmcldeps
			false,
			configName);
		
		lg->AppendFlags(compileFlags, includeFlags);

		// Append old-style preprocessor definition flags.
		lg->AppendFlags(compileFlags,
			lg->GetMakefile()->GetDefineFlags());

		// Add target-specific flags.
		lg->AddCompileOptions(compileFlags, 
			&target,
			language,
			configName);

		if (source)
		{
			lg->AppendFlags(compileFlags, source->GetProperty("COMPILE_FLAGS"));
		}
	}

	static void DetectTargetCompileDependencies(
		cmGlobalFastbuildGenerator* gg,
		cmTarget& target, 
		std::vector<std::string>& dependencies)
	{
		if (target.GetType() == cmTarget::GLOBAL_TARGET)
		{
			// Global targets only depend on other utilities, which may not appear in
			// the TargetDepends set (e.g. "all").
			std::set<std::string> const& utils = target.GetUtilities();
			std::copy(utils.begin(), utils.end(), std::back_inserter(dependencies));
		}
		else 
		{
			cmTargetDependSet const& targetDeps =
				gg->GetTargetDirectDepends(target);
			for (cmTargetDependSet::const_iterator i = targetDeps.begin();
				i != targetDeps.end(); ++i)
			{
				const cmTargetDepend& depTarget = *i;
				if (depTarget->GetType() == cmTarget::INTERFACE_LIBRARY)
				{
					continue;
				}
				dependencies.push_back(depTarget->GetName());
			}
		}
	}

	static void DetectTargetLinkDependencies(
		cmTarget& target,
		const std::string& configName,
		std::vector<std::string>& dependencies)
	{
		// Static libraries never depend on other targets for linking.
		if (target.GetType() == cmTarget::STATIC_LIBRARY ||
			target.GetType() == cmTarget::OBJECT_LIBRARY)
		{
			return;
		}

		cmComputeLinkInformation* cli =
			target.GetLinkInformation(configName);
		if(!cli)
		{
			return;
		}

		const std::vector<std::string> &deps = cli->GetDepends();
		std::copy(deps.begin(), deps.end(), std::back_inserter(dependencies));
	}

	static std::string DetectTargetCompileOutputDir(
		cmLocalFastbuildGenerator* lg,
		const cmTarget& target,
		std::string configName)
	{
		std::string result = lg->GetTargetDirectory(target) + "/";
		if (!configName.empty())
		{
			result = result + configName + "/";
		}
		cmSystemTools::ConvertToOutputSlashes(result);
		return result;
	}

	static void DetectTargetObjectDependencies(
		cmGlobalFastbuildGenerator* gg,
		cmTarget& target, 
		const std::string& configName,
		std::vector<std::string>& dependencies)
	{
		// Iterate over all source files and look for 
		// object file dependencies
		cmGeneratorTarget *gt = gg->GetGeneratorTarget(&target);

		std::set<std::string> objectLibs;

		std::vector<cmSourceFile*> sourceFiles;
		gt->GetSourceFiles(sourceFiles, configName);
		for (std::vector<cmSourceFile*>::const_iterator
			i = sourceFiles.begin(); i != sourceFiles.end(); ++i)
		{
			const std::string& objectLib = (*i)->GetObjectLibrary();
			if (!objectLib.empty())
			{
				// Find the target this actually is (might be an alias)
				const cmTarget* objectTarget = gg->FindTarget(objectLib);
				if (objectTarget)
				{
					objectLibs.insert(objectTarget->GetName() + "-" + configName + "-products");
				}
			}
		}

		std::copy(objectLibs.begin(), objectLibs.end(),
			std::back_inserter(dependencies) );

		// Now add the external obj files that also need to be linked in
		std::vector<const cmSourceFile*> objFiles;
		gt->GetExternalObjects(objFiles, configName);
		for (std::vector<const cmSourceFile*>::const_iterator
			i = objFiles.begin(); i != objFiles.end(); ++i)
		{
			const cmSourceFile* sourceFile = *i;
			if (sourceFile->GetObjectLibrary().empty())
			{
				dependencies.push_back(sourceFile->GetFullPath());
			}
		}
	}

	struct DependencySorter
	{
		struct TargetHelper
		{
			cmGlobalFastbuildGenerator *gg;

			void GetOutputs(const cmTarget* entry, std::vector<std::string>& outputs)
			{
				outputs.push_back(entry->GetName());
			}

			void GetInputs(const cmTarget* entry, std::vector<std::string>& inputs)
			{
				TargetDependSet const& ts = gg->GetTargetDirectDepends(*entry);
				for (TargetDependSet::const_iterator iter = ts.begin(); iter != ts.end(); ++iter)
				{
					const cmTarget * dtarget = *iter;
					inputs.push_back(dtarget->GetName());
				}
			}
		};
		
		struct CustomCommandHelper
		{
			cmGlobalFastbuildGenerator *gg;
			cmLocalFastbuildGenerator *lg;
			const std::string& configName;

			void GetOutputs(const cmSourceFile* entry, std::vector<std::string>& outputs)
			{
				const cmCustomCommand* cc = entry->GetCustomCommand();
				cmMakefile* makefile = lg->GetMakefile();

				// We need to generate the command for execution.
				cmCustomCommandGenerator ccg(*cc, configName, makefile);

				const std::vector<std::string> &ccOutputs = ccg.GetOutputs();
				const std::vector<std::string> &byproducts = ccg.GetByproducts();
				outputs.insert(outputs.end(), ccOutputs.begin(), ccOutputs.end());
				outputs.insert(outputs.end(), byproducts.begin(), byproducts.end());
			}

			void GetInputs(const cmSourceFile* entry, std::vector<std::string>& inputs)
			{
				const cmCustomCommand* cc = entry->GetCustomCommand();
				cmMakefile* makefile = lg->GetMakefile();
				cmCustomCommandGenerator ccg(*cc, configName, makefile);

				std::string workingDirectory = ccg.GetWorkingDirectory();
				if (workingDirectory.empty())
				{
					workingDirectory = makefile->GetCurrentOutputDirectory();
					workingDirectory += "/";
				}

				// Take the dependencies listed and split into targets and files.
				const std::vector<std::string> &depends = ccg.GetDepends();
				for (std::vector<std::string>::const_iterator iter = depends.begin();
					iter != depends.end(); ++iter)
				{
					std::string dep = *iter;

					bool isTarget = gg->FindTarget(dep) != NULL;
					if (!isTarget)
					{
						if (!cmSystemTools::FileIsFullPath(dep.c_str()))
						{
							dep = workingDirectory + dep;
						}

						inputs.push_back(dep);
					}
				}
			}
		};

		template <class TType, class TTypeHelper>
		static void Sort(TTypeHelper& helper, std::vector<const TType*>& entries)
		{
			typedef std::vector<std::string> StringVector;
			typedef std::vector<const TType*> OrderedEntrySet;
			typedef std::map<std::string, const TType*> OutputMap;

			// Build up a map of outputNames to entries
			OutputMap outputMap;
			for (typename OrderedEntrySet::iterator iter = entries.begin();
				iter != entries.end();
				++iter)
			{
				const TType* entry = *iter;
				StringVector outputs;
				helper.GetOutputs(entry, outputs);

				for (StringVector::iterator outIter = outputs.begin();
					outIter != outputs.end();
					++outIter)
				{
					outputMap[*outIter] = entry;
				}
			}

			// Now build a forward and reverse map of dependencies
			// Build the reverse graph, 
			// each target, and the set of things that depend upon it
			typedef std::map<const TType*, std::vector<const TType*> > DepMap;
			DepMap forwardDeps;
			DepMap reverseDeps;
			for (typename OrderedEntrySet::iterator iter = entries.begin();
				iter != entries.end();
				++iter)
			{
				const TType* entry = *iter;
				std::vector<const TType*>& entryInputs = forwardDeps[entry];

				StringVector inputs;
				helper.GetInputs(entry, inputs);
				for (StringVector::const_iterator inIter = inputs.begin(); 
					inIter != inputs.end(); 
					++inIter)
				{
					const std::string& input = *inIter;
					// Lookup the input in the output map and find the right entry
					typename OutputMap::iterator findResult = outputMap.find(input);
					if (findResult != outputMap.end())
					{
						const TType* dentry = findResult->second;
						entryInputs.push_back(dentry);
						reverseDeps[dentry].push_back(entry);
					}
				}
			}

			// We have all the information now.
			// Clear the array passed in
			entries.clear();

			// Now iterate over each target with its list of dependencies.
			// And dump out ones that have 0 dependencies.
			bool written = true;
			while (!forwardDeps.empty() && written)
			{
				written = false;
				for (typename DepMap::iterator iter = forwardDeps.begin();
					iter != forwardDeps.end(); ++iter)
				{
					std::vector<const TType*>& fwdDeps = iter->second;
					const TType* entry = iter->first;
					if (!fwdDeps.empty())
					{
						// Looking for empty dependency lists.
						// Those are the next to be written out
						continue;
					}

					// dependency list is empty,
					// add it to the output list
					written = true;
					entries.push_back(entry);

					// Use reverse dependencies to determine 
					// what forward dep lists to adjust
					std::vector<const TType*>& revDeps = reverseDeps[entry];
					for (unsigned int i = 0; i < revDeps.size(); ++i)
					{
						const TType* revDep = revDeps[i];

						// Fetch the list of deps on that target
						std::vector<const TType*>& revDepFwdDeps =
							forwardDeps[revDep];
						// remove the one we just added from the list
						revDepFwdDeps.erase(
							std::remove(revDepFwdDeps.begin(), revDepFwdDeps.end(), entry),
							revDepFwdDeps.end());
					}

					// Remove it from forward deps so not
					// considered again
					forwardDeps.erase(entry);

					// Must break now as we've invalidated
					// our forward deps iterator
					break;
				}
			}

			// Validation...
			// Make sure we managed to find a place
			// to insert every dependency.
			// If this fires, then there is most likely
			// a cycle in the graph...
			assert(forwardDeps.empty());
		}
	};

	typedef std::vector<const cmTarget*> OrderedTargetSet;
	static void ComputeTargetOrderAndDependencies(
		cmGlobalFastbuildGenerator* gg,
		OrderedTargetSet& orderedTargets)
	{
		TargetDependSet projectTargets;
		TargetDependSet originalTargets;
		std::map<std::string, std::vector<cmLocalGenerator*> >::const_iterator
			it = gg->GetProjectMap().begin(),
			end = gg->GetProjectMap().end();
		for(; it != end; ++it)
		{
			const std::vector<cmLocalGenerator*>& generators = it->second;
			cmLocalFastbuildGenerator* root =
				static_cast<cmLocalFastbuildGenerator*>(generators[0]);
			
			// Given this information, calculate the dependencies:
			// Collect all targets under this root generator and the transitive
			// closure of their dependencies.
			
			gg->GetTargetSets(projectTargets, originalTargets, root, generators);
		}

		// Iterate over the targets and export their order
		for (TargetDependSet::iterator iter = projectTargets.begin();
			iter != projectTargets.end();
			++iter)
		{
			const cmTargetDepend& targetDepend = *iter;
			const cmTarget& target = *targetDepend;

			orderedTargets.push_back(&target);
		}

		DependencySorter::TargetHelper targetHelper = {gg};
		DependencySorter::Sort(targetHelper, orderedTargets);
	}

	// Iterate over all targets and remove the ones that are
	// not needed for generation.
	// i.e. the nested global targets
	struct RemovalTest
	{
		bool operator()(const cmTarget* target) const
		{
			if (target->GetType() == cmTarget::GLOBAL_TARGET)
			{
				// We only want to process global targets that live in the home
				// (i.e. top-level) directory.  CMake creates copies of these targets
				// in every directory, which we don't need.
				cmMakefile *mf = target->GetMakefile();
				if (strcmp(mf->GetStartDirectory(), mf->GetHomeDirectory()) != 0)
				{
					return true;
				}
			}
			return false;
		}
	};

	static void StripNestedGlobalTargets( OrderedTargetSet& orderedTargets )
	{
		orderedTargets.erase(
			std::remove_if(orderedTargets.begin(), orderedTargets.end(), RemovalTest()),
			orderedTargets.end());
	}

	static bool isConfigDependant(const cmCustomCommandGenerator& ccg)
	{
		typedef std::vector<std::string> StringVector;
		StringVector outputs = ccg.GetOutputs();
		StringVector byproducts = ccg.GetByproducts();

		std::for_each(outputs.begin(), outputs.end(), &Detection::UnescapeFastbuildVariables);
		std::for_each(byproducts.begin(), byproducts.end(), &Detection::UnescapeFastbuildVariables);
		
		// Make sure that the outputs don't depend on the config name
		for (StringVector::const_iterator iter = outputs.begin();
			iter != outputs.end();
			++iter)
		{
			const std::string & str = *iter;
			if (str.find("$ConfigName$") != std::string::npos)
			{
				return true;
			}
		}
		for (StringVector::const_iterator iter = byproducts.begin();
			iter != byproducts.end();
			++iter)
		{
			const std::string & str = *iter;
			if (str.find("$ConfigName$") != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	static void DetectCompilerExtraFiles(const std::string& compilerID,
		const std::string& version, std::vector<std::string>& extraFiles)
	{
		// Output a list of files that are relative to $CompilerRoot$
		return;

		if (compilerID == "MSVC")
		{
			if (version.compare(0, 3, "18.") != std::string::npos)
			{
				// Using vs2013
				const char *vs2013_extraFiles[13] = {
					"$CompilerRoot$\\c1.dll",
					"$CompilerRoot$\\c1ast.dll",
					"$CompilerRoot$\\c1xx.dll",
					"$CompilerRoot$\\c1xxast.dll",
					"$CompilerRoot$\\c2.dll",
					"$CompilerRoot$\\msobj120.dll",
					"$CompilerRoot$\\mspdb120.dll",
					"$CompilerRoot$\\mspdbcore.dll",
					"$CompilerRoot$\\mspft120.dll",
					"$CompilerRoot$\\1033\\clui.dll",
					"$CompilerRoot$\\..\\..\\redist\\x86\\Microsoft.VC120.CRT\\msvcp120.dll",
					"$CompilerRoot$\\..\\..\\redist\\x86\\Microsoft.VC120.CRT\\msvcr120.dll",
					"$CompilerRoot$\\..\\..\\redist\\x86\\Microsoft.VC120.CRT\\vccorlib120.dll"
				};
				extraFiles.insert(extraFiles.end(), &vs2013_extraFiles[0], &vs2013_extraFiles[13]);
			}
			else if (version.compare(0, 3, "17.") != std::string::npos)
			{
				// Using vs2012
				const char *vs2012_extraFiles[12] = {
					"$CompilerRoot$\\c1.dll",
					"$CompilerRoot$\\c1ast.dll",
					"$CompilerRoot$\\c1xx.dll",
					"$CompilerRoot$\\c1xxast.dll",
					"$CompilerRoot$\\c2.dll",
					"$CompilerRoot$\\mspft110.dll",
					"$CompilerRoot$\\1033\\clui.dll",
					"$CompilerRoot$\\..\\..\\redist\\x86\\Microsoft.VC110.CRT\\msvcp110.dll",
					"$CompilerRoot$\\..\\..\\redist\\x86\\Microsoft.VC110.CRT\\msvcr110.dll",
					"$CompilerRoot$\\..\\..\\redist\\x86\\Microsoft.VC110.CRT\\vccorlib110.dll",
					"$CompilerRoot$\\..\\..\\..\\Common7\\IDE\\mspdb110.dll",
					"$CompilerRoot$\\..\\..\\..\\Common7\\IDE\\mspdbcore.dll"
				};
				extraFiles.insert(extraFiles.end(), &vs2012_extraFiles[0], &vs2012_extraFiles[12]);
			}
		}
	}

private:

};

//----------------------------------------------------------------------------
class cmGlobalFastbuildGenerator::Detail::Definition
{
	struct FastbuildSettings
	{
		std::string cachePath;
	};

	struct FastbuildCompiler
	{
		std::string executable;
	};
	typedef std::map<std::string, FastbuildCompiler> CompilerMap;

	struct FastbuildConfiguration
	{
		std::string name;
	};
	typedef std::map<std::string, FastbuildConfiguration> ConfigurationMap;

	struct FastbuildStructure
	{
		ConfigurationMap configurations;

		FastbuildSettings settings;
		CompilerMap compilers;
	};
};

//----------------------------------------------------------------------------
class cmGlobalFastbuildGenerator::Detail::Generation
{
public:
	struct TargetGenerationContext
	{
		cmTarget* target;
		cmLocalFastbuildGenerator* root;
		std::vector<cmLocalGenerator*> generators;
		cmLocalFastbuildGenerator* lg;
	};
	typedef std::map<const cmTarget*, TargetGenerationContext> TargetContextMap;
	typedef std::map<const cmCustomCommand*, std::set<std::string> > CustomCommandAliasMap;
	typedef Detection::OrderedTargetSet OrderedTargets;

	struct GenerationContext
	{
		GenerationContext(
				cmGlobalFastbuildGenerator * globalGen,
				cmLocalFastbuildGenerator* localGen,
				FileContext& fileCtx)
			: self(globalGen)
			, root(localGen)
			, fc(fileCtx)
		{}
		cmGlobalFastbuildGenerator * self;
		cmLocalFastbuildGenerator* root;
		FileContext& fc;
		OrderedTargets orderedTargets;
		TargetContextMap targetContexts;
		CustomCommandAliasMap customCommandAliases;
	};

	static std::string Quote(const std::string& str, const std::string& quotation = "'")
	{
		std::string result = str;
		cmSystemTools::ReplaceString(result, quotation, "^" + quotation);
		return quotation + result + quotation;
	}

	static std::string Join(const std::vector<std::string>& elems, 
		const std::string& delim)
	{
		std::stringstream stringstream;
		for (std::vector<std::string>::const_iterator iter = elems.begin(); 
			iter != elems.end(); ++iter)
		{
			stringstream << (*iter);
			if (iter + 1 != elems.end()) {
				stringstream << delim;
			}
		}

		return stringstream.str();
	}

	struct WrapHelper
	{
		std::string m_prefix;
		std::string m_suffix;

		std::string operator()(const std::string& in)
		{
			return m_prefix + in + m_suffix;
		}
	};

	static std::vector<std::string> Wrap(const std::vector<std::string>& in, const std::string& prefix = "'", const std::string& suffix = "'")
	{
		std::vector<std::string> result;

		WrapHelper helper = {prefix, suffix};

		std::transform(in.begin(), in.end(),
			std::back_inserter(result), helper);

		return result;
	}

	static std::string EncodeLiteral(const std::string &lit)
	{
		std::string result = lit;
		cmSystemTools::ReplaceString(result, "$", "^$");
		return result;
	}

	static void EnsureDirectoryExists(const std::string& path,
		GenerationContext& context)
	{
		if (cmSystemTools::FileIsFullPath(path.c_str()))
		{
			cmSystemTools::MakeDirectory(path.c_str());
		}
		else
		{
			const std::string fullPath = std::string(
				context.self->GetCMakeInstance()->GetHomeOutputDirectory())
				+ "/" + path;
			cmSystemTools::MakeDirectory(fullPath.c_str());
		}
	}
	
	static void BuildTargetContexts(cmGlobalFastbuildGenerator * gg,
		TargetContextMap& map)
	{
		std::map<std::string, std::vector<cmLocalGenerator*> >::const_iterator
				it = gg->GetProjectMap().begin(),
				end = gg->GetProjectMap().end();
		for(; it != end; ++it)
		{
			const std::vector<cmLocalGenerator*>& generators = it->second;
			cmLocalFastbuildGenerator* root =
				static_cast<cmLocalFastbuildGenerator*>(generators[0]);

			// Build a map of all targets to their local generator
			for (std::vector<cmLocalGenerator*>::const_iterator iter = generators.begin();
				iter != generators.end(); ++iter)
			{
				cmLocalFastbuildGenerator *lg = static_cast<cmLocalFastbuildGenerator*>(*iter);

				if(gg->IsExcluded(root, lg))
				{
					continue;
				}

				cmTargets &tgts = lg->GetMakefile()->GetTargets();
				for (cmTargets::iterator targetIter = tgts.begin(); 
					targetIter != tgts.end();
					++targetIter)
				{
					cmTarget &target = (targetIter->second);

					if(gg->IsRootOnlyTarget(&target) &&
						target.GetMakefile() != root->GetMakefile())
					{
						continue;
					}

					TargetGenerationContext targetContext =
						{ &target, root, generators, lg };
					map[&target] = targetContext;
				}
			}
		}
	}

	static void GenerateRootBFF(cmGlobalFastbuildGenerator * self)
	{

		cmLocalFastbuildGenerator* root = 
			static_cast<cmLocalFastbuildGenerator*>(self->GetLocalGenerators()[0]);

		// Calculate filename
		std::string fname = root->GetMakefile()->GetStartOutputDirectory();
		fname += "/";
		//fname += root->GetMakefile()->GetProjectName();
		fname += "fbuild";
		fname += ".bff";
		
		// Open file
		cmGeneratedFileStream fout(fname.c_str());
		fout.SetCopyIfDifferent(true);
		if(!fout)
		{
			return;
		}

		FileContext fc(fout);
		GenerationContext context(self, root, fc);
		Detection::ComputeTargetOrderAndDependencies( context.self, context.orderedTargets );
		Detection::StripNestedGlobalTargets( context.orderedTargets );
		BuildTargetContexts( context.self, context.targetContexts );
		WriteRootBFF(context);
		
		// Close file
		if (fout.Close())
		{
			self->FileReplacedDuringGenerate(fname);
		}
	}

	static void WriteRootBFF(GenerationContext& context)
	{
		context.fc.WriteSectionHeader("Fastbuild makefile - Generated using CMAKE");

		WritePlaceholders( context );
		WriteSettings( context );
		WriteCompilers( context );
		WriteConfigurations( context );

		// Sort targets

		WriteTargetDefinitions( context, false );
		WriteAliases( context, false );
		WriteTargetDefinitions( context, true );
		WriteAliases( context, true );
	}

	static void WritePlaceholders(GenerationContext& context)
	{
		// Define some placeholder 
		context.fc.WriteSectionHeader("Helper variables");

		context.fc.WriteVariable( "FB_INPUT_1_PLACEHOLDER", Quote("\"%1\"") );
		context.fc.WriteVariable( "FB_INPUT_2_PLACEHOLDER", Quote("\"%2\"") );
	}

	static void WriteSettings( GenerationContext& context )
	{
		context.fc.WriteSectionHeader("Settings");

		context.fc.WriteCommand("Settings");
		context.fc.WritePushScope();

		std::string cacheDir =
			context.self->GetCMakeInstance()->GetHomeOutputDirectory();
		cacheDir += "\\.fbuild.cache";
		cmSystemTools::ConvertToOutputSlashes(cacheDir);

		context.fc.WriteVariable("CachePath", Quote(cacheDir));
		context.fc.WritePopScope();
	}

	struct CompilerDef
	{
		std::string name;
		std::string path;
		std::string cmakeCompilerID;
		std::string cmakeCompilerVersion;
	};

	static bool WriteCompilers( GenerationContext& context )
	{
		cmMakefile *mf = context.root->GetMakefile();

		context.fc.WriteSectionHeader("Compilers");

		// Detect each language used in the definitions
		std::set<std::string> languages;
		for (TargetContextMap::iterator iter = context.targetContexts.begin();
			iter != context.targetContexts.end(); ++iter)
		{
			TargetGenerationContext& targetContext = iter->second;

			if (targetContext.target->GetType() == cmTarget::INTERFACE_LIBRARY)
			{
				continue;
			}

			Detection::DetectLanguages(languages, context.self,
				*targetContext.target);
		}

		// Now output a compiler for each of these languages
		typedef std::map<std::string, std::string> StringMap;
		typedef std::map<std::string, CompilerDef> CompilerDefMap;
		CompilerDefMap compilerToDef;
		StringMap languageToCompiler;
		for (std::set<std::string>::iterator iter = languages.begin();
			iter != languages.end();
			++iter)
		{
			const std::string & language = *iter;

			// Calculate the root location of the compiler
			std::string variableString = "CMAKE_"+language+"_COMPILER";
			std::string compilerLocation = mf->GetSafeDefinition(variableString);
			if (compilerLocation.empty())
			{
				return false;
			}

			// Add the language to the compiler's name
			CompilerDef& compilerDef = compilerToDef[compilerLocation];
			if (compilerDef.name.empty())
			{
				compilerDef.name = "Compiler";
				compilerDef.path = compilerLocation;
				compilerDef.cmakeCompilerID = 
					mf->GetSafeDefinition("CMAKE_" + language + "_COMPILER_ID");
				compilerDef.cmakeCompilerVersion =
					mf->GetSafeDefinition("CMAKE_" + language + "_COMPILER_VERSION");
			}
			compilerDef.name += "-";
			compilerDef.name += language;

			// Now add the language to point to that compiler location
			languageToCompiler[language] = compilerLocation;
		}

		// Now output all the compilers
		for (CompilerDefMap::iterator iter = compilerToDef.begin();
			iter != compilerToDef.end();
			++iter)
		{
			const CompilerDef& compilerDef = iter->second;

			// Detect the list of extra files used by this compiler
			// for distribution
			std::vector<std::string> extraFiles;
			Detection::DetectCompilerExtraFiles(compilerDef.cmakeCompilerID,
				compilerDef.cmakeCompilerVersion, extraFiles);

			// Strip out the path to the compiler
			std::string compilerPath = 
				cmSystemTools::GetFilenamePath(compilerDef.path);
			std::string compilerFile = "$CompilerRoot$\\" +
				cmSystemTools::GetFilenameName(compilerDef.path);

			cmSystemTools::ConvertToOutputSlashes(compilerPath);
			cmSystemTools::ConvertToOutputSlashes(compilerFile);

			// Write out the compiler that has been configured
			context.fc.WriteCommand("Compiler", Quote(compilerDef.name));
			context.fc.WritePushScope();
			context.fc.WriteVariable("CompilerRoot", Quote(compilerPath));
			context.fc.WriteVariable("Executable", Quote(compilerFile));
			context.fc.WriteArray("ExtraFiles", Wrap(extraFiles));

			context.fc.WritePopScope();
		}

		// Now output the compiler names according to language as variables
		for (StringMap::iterator iter = languageToCompiler.begin();
			iter != languageToCompiler.end();
			++iter)
		{
			const std::string& language = iter->first;
			const std::string& compilerLocation = iter->second;
			const CompilerDef& compilerDef = compilerToDef[compilerLocation];

			// Output a default compiler to absorb the library requirements for a compiler
			if (iter == languageToCompiler.begin())
			{
				context.fc.WriteVariable("Compiler_dummy", Quote(compilerDef.name));
			}

			context.fc.WriteVariable("Compiler_" + language, Quote(compilerDef.name));
		}
		
		return true;
	}
	
	static void WriteConfigurations(GenerationContext& context)
	{
		context.fc.WriteSectionHeader("Configurations");

		context.fc.WriteVariable("ConfigBase", "");
		context.fc.WritePushScopeStruct();
		context.fc.WritePopScope();

		// Iterate over all configurations and define them:
		std::vector<std::string>::const_iterator
				iter = context.self->GetConfigurations().begin(),
				end = context.self->GetConfigurations().end();
		for (; iter != end; ++iter)
		{
			const std::string & configName = *iter;
			context.fc.WriteVariable("config_" + configName, "");
			context.fc.WritePushScopeStruct();

			// Using base config
			context.fc.WriteCommand("Using", ".ConfigBase");

			context.fc.WritePopScope();
		}

		// Write out a list of all configs
		context.fc.WriteArray("all_configs", 
			Wrap(context.self->GetConfigurations(), ".config_", ""));
	}

	static void WriteCustomCommand(
		GenerationContext& context,
		const cmCustomCommand* cc,
		cmLocalFastbuildGenerator *lg,
		cmTarget& target,
		const std::string& configName,
		std::string& targetName,
		const std::string& hostTargetName)
	{
		cmMakefile* makefile = lg->GetMakefile();
		
		// We need to generate the command for execution.
		cmCustomCommandGenerator ccg(*cc, configName, makefile);

		const std::vector<std::string> &outputs = ccg.GetOutputs();
		const std::vector<std::string> &byproducts = ccg.GetByproducts();
		std::vector<std::string> mergedOutputs;
		mergedOutputs.insert(mergedOutputs.end(), outputs.begin(), outputs.end());
		mergedOutputs.insert(mergedOutputs.end(), byproducts.begin(), byproducts.end());

		// TODO: Double check that none of the outputs are 'symbolic'
		// In which case, FASTBuild won't want them treated as 
		// outputs.
		{
			// Loop through all outputs, and attempt to find it in the 
			// source files.
			for (size_t index = 0; index < mergedOutputs.size(); ++index)
			{
				const std::string& outputName = mergedOutputs[index];

				cmSourceFile* outputSourceFile = makefile->GetSource(outputName);
				// Check if this file is symbolic
				if (outputSourceFile && outputSourceFile->GetPropertyAsBool("SYMBOLIC"))
				{
					// We need to remove this file from the list of outputs
					// Swap with back and pop
					mergedOutputs[index] = mergedOutputs.back();
					mergedOutputs.pop_back();
				}
			}
		}

		std::vector<std::string> inputs;
		std::vector<std::string> orderDependencies;

		// If this exec node always generates outputs,
		// then we need to make sure we don't define outputs multiple times.
		// but if the command should always run (i.e. post builds etc)
		// then we will output a new one.
		if (!mergedOutputs.empty())
		{
			// Check if this custom command has already been output.
			// If it has then just drop an alias here to the original
			CustomCommandAliasMap::iterator findResult = context.customCommandAliases.find(cc);
			if (findResult != context.customCommandAliases.end())
			{
				const std::set<std::string>& aliases = findResult->second;
				if (aliases.find(targetName) != aliases.end())
				{
					// This target has already been generated
					// with the correct name somewhere else.
					return;
				}	
				if (!Detection::isConfigDependant(ccg))
				{
					// This command has already been generated.
					// But under a different name so setup an alias to redirect
					// No merged outputs, so this command must always be run.
					// Make it's name unique to its host target
					targetName += "-";
					targetName += hostTargetName;

					std::vector<std::string> targets;
					targets.push_back(*findResult->second.begin());

					context.fc.WriteCommand("Alias", Quote(targetName));
					context.fc.WritePushScope();
					{
						context.fc.WriteArray("Targets",
							Wrap(targets));
					}
					context.fc.WritePopScope();
					return;
				}
			}
			context.customCommandAliases[cc].insert(targetName);
		}
		else
		{
			// No merged outputs, so this command must always be run.
			// Make it's name unique to its host target
			targetName += "-";
			targetName += hostTargetName;
		}
		
		// Take the dependencies listed and split into targets and files.
		const std::vector<std::string> &depends = ccg.GetDepends();
		for (std::vector<std::string>::const_iterator iter = depends.begin();
			iter != depends.end(); ++iter)
		{
			const std::string& dep = *iter;

			bool isTarget = context.self->FindTarget(dep) != NULL;
			if (isTarget)
			{
				orderDependencies.push_back(dep + "-" + configName);
			}
			else
			{
				inputs.push_back(dep);
			}
		}

#ifdef _WIN32
		const std::string shellExt = ".bat";
#else
		const std::string shellExt = ".sh";
#endif

		std::string workingDirectory = ccg.GetWorkingDirectory();
		if (workingDirectory.empty())
		{
			workingDirectory = makefile->GetCurrentOutputDirectory();
			workingDirectory += "/";
		}

		std::string scriptFileName(workingDirectory + targetName + ".bat");
		cmsys::ofstream scriptFile(scriptFileName.c_str());

		for (unsigned i = 0; i != ccg.GetNumberOfCommands(); ++i) 
		{
			std::string args;
			ccg.AppendArguments(i, args);
			cmSystemTools::ReplaceString(args, "$$", "$");
			cmSystemTools::ReplaceString(args, FASTBUILD_DOLLAR_TAG, "$");
#ifdef _WIN32
			//in windows batch, '%' is a special character that needs to be doubled to be escaped
			cmSystemTools::ReplaceString(args, "%", "%%");
#endif
			Detection::ResolveFastbuildVariables(args, configName);

			std::string command(ccg.GetCommand(i));
			cmSystemTools::ReplaceString(command, FASTBUILD_DOLLAR_TAG, "$");
			Detection::ResolveFastbuildVariables(command, configName);

			scriptFile << Quote(command, "\"") << args << std::endl;
		}

		// Write out an exec command
		/*
		Exec(alias); (optional)Alias
		{
			.ExecExecutable; Executable to run
			.ExecInput; Input file to pass to executable
			.ExecOutput; Output file generated by executable
			.ExecArguments; (optional)Arguments to pass to executable
			.ExecWorkingDir; (optional)Working dir to set for executable

			; Additional options
			.PreBuildDependencies; (optional)Force targets to be built before this Exec(Rarely needed,
			; but useful when Exec relies on externally generated files).
		}
		*/

		std::for_each(inputs.begin(), inputs.end(), &Detection::UnescapeFastbuildVariables);
		std::for_each(mergedOutputs.begin(), mergedOutputs.end(), &Detection::UnescapeFastbuildVariables);

		context.fc.WriteCommand("Exec", Quote(targetName));
		context.fc.WritePushScope();
		{
#ifdef _WIN32
			context.fc.WriteVariable("ExecExecutable", Quote(cmSystemTools::FindProgram("cmd.exe")));
			context.fc.WriteVariable("ExecArguments", Quote("/C " + scriptFileName));
#else
			context.fc.WriteVariable("ExecExecutable", Quote(scriptFileName));
#endif
			if(!workingDirectory.empty())
			{
				context.fc.WriteVariable("ExecWorkingDir", Quote(workingDirectory));
			}

			if (inputs.empty())
			{
				//inputs.push_back("dummy-in");
			}
			context.fc.WriteArray("ExecInput", Wrap(inputs));

			if (mergedOutputs.empty())
			{
				context.fc.WriteVariable("ExecUseStdOutAsOutput", "true");

				std::string outputDir = target.GetMakefile()->GetStartOutputDirectory();
				mergedOutputs.push_back(outputDir + "/dummy-out-" + targetName + ".txt");
			}
			// Currently fastbuild doesn't support more than 1
			// output for a custom command (soon to change hopefully).
			// so only use the first one
			context.fc.WriteVariable("ExecOutput", Quote(mergedOutputs[0]));
			
		}
		context.fc.WritePopScope();
	}

	static void WriteCustomBuildSteps(
		GenerationContext& context,
		cmLocalFastbuildGenerator *lg,
		cmTarget &target,
		const std::vector<cmCustomCommand>& commands,
		const std::string& buildStep,
		const std::vector<std::string>& orderDeps)
	{
		if (commands.empty())
		{
			return;
		}

		const std::string& targetName = target.GetName();

		// Now output the commands
		std::vector<std::string>::const_iterator
				iter = context.self->GetConfigurations().begin(),
				end = context.self->GetConfigurations().end();
		for (; iter != end; ++iter)
		{
			const std::string & configName = *iter;

			context.fc.WriteVariable("buildStep_" + buildStep + "_" + configName, "");
			context.fc.WritePushScopeStruct();

			context.fc.WriteCommand("Using", ".BaseConfig_" + configName);

			context.fc.WriteArray("PreBuildDependencies",
				Wrap(orderDeps, "'", "-" + configName + "'"),
				"+");

			std::string baseName = targetName + "-" + buildStep + "-" + configName;
			int commandCount = 1;
			std::vector<std::string> customCommandTargets;
			for (std::vector<cmCustomCommand>::const_iterator ccIter = commands.begin();
				ccIter != commands.end(); ++ccIter)
			{
				const cmCustomCommand& cc = *ccIter;

				std::stringstream customCommandTargetName;
				customCommandTargetName << baseName << (commandCount++);
				
				std::string customCommandTargetNameStr = customCommandTargetName.str();
				WriteCustomCommand(context, &cc, lg, target, configName, customCommandTargetNameStr,
					targetName);
				customCommandTargets.push_back(customCommandTargetNameStr);
			}

			// Write an alias for this object group to group them all together
			context.fc.WriteCommand("Alias", Quote(baseName));
			context.fc.WritePushScope();
			context.fc.WriteArray("Targets",
				Wrap(customCommandTargets, "'", "'"));
			context.fc.WritePopScope();

			context.fc.WritePopScope();
		}
	}

	static bool WriteCustomBuildRules(
		GenerationContext& context, 
		cmLocalFastbuildGenerator *lg, 
		cmGeneratorTarget *gt,
		cmTarget &target)
	{
		bool hasCustomCommands = false;
		const std::string& targetName = target.GetName();

		// Iterating over all configurations
		std::vector<std::string>::const_iterator
			iter = context.self->GetConfigurations().begin(),
			end = context.self->GetConfigurations().end();
		for (; iter != end; ++iter)
		{
			std::string configName = *iter;

			context.fc.WriteVariable("CustomCommands_" + configName, "");
			context.fc.WritePushScopeStruct();

			context.fc.WriteCommand("Using", ".BaseConfig_" + configName);

			// Figure out the list of custom build rules in use by this target
			// get a list of source files
			std::vector<cmSourceFile const*> customCommands;
			gt->GetCustomCommands(customCommands, configName);

			if (!customCommands.empty())
			{
				// Presort the commands to adjust for dependencies
				// In a number of cases, the commands inputs will be the outputs
				// from another command. Need to sort the commands to output them in order.
				Detection::DependencySorter::CustomCommandHelper ccHelper =
					{ context.self, lg, configName };
				Detection::DependencySorter::Sort(ccHelper, customCommands);

				std::vector<std::string> customCommandTargets;

				// Write the custom command build rules for each configuration
				int commandCount = 1;
				std::string customCommandNameBase = "CustomCommand-" + configName + "-";
				for (std::vector<cmSourceFile const*>::iterator ccIter = customCommands.begin();
					ccIter != customCommands.end(); ++ccIter)
				{
					const cmSourceFile* sourceFile = *ccIter;

					std::stringstream customCommandTargetName;
					customCommandTargetName << customCommandNameBase << (commandCount++);
					customCommandTargetName << "-" << cmSystemTools::GetFilenameName(sourceFile->GetFullPath());;
					
					std::string customCommandTargetNameStr = customCommandTargetName.str();
					WriteCustomCommand(context, sourceFile->GetCustomCommand(),
						lg, target, configName, customCommandTargetNameStr, 
						targetName);

					customCommandTargets.push_back(customCommandTargetNameStr);
				}

				std::string customCommandGroupName = targetName + "-CustomCommands-" + configName;

				// Write an alias for this object group to group them all together
				context.fc.WriteCommand("Alias", Quote(customCommandGroupName));
				context.fc.WritePushScope();
				context.fc.WriteArray("Targets",
					Wrap(customCommandTargets, "'", "'"));
				context.fc.WritePopScope();

				// Now make everything use this as prebuilt dependencies
				std::vector<std::string> tmp;
				tmp.push_back(customCommandGroupName);
				context.fc.WriteArray("PreBuildDependencies",
					Wrap(tmp),
					"+");

				hasCustomCommands = true;
			}

			context.fc.WritePopScope();
		}

		return hasCustomCommands;
	}

	struct CompileCommand
	{
		std::string defines;
		std::string flags;
		std::map<std::string, std::vector<std::string> > sourceFiles;
	};

	static void WriteTargetDefinition(GenerationContext& context,
		cmLocalFastbuildGenerator *lg, cmTarget &target)
	{
		// Detection of the link command as follows:
		std::string linkCommand = "Library";
		switch (target.GetType())
		{
			case cmTarget::INTERFACE_LIBRARY:
				// We don't write out interface libraries.
				return;
			case cmTarget::EXECUTABLE:
			{
				linkCommand = "Executable";
				break;
			}
			case cmTarget::SHARED_LIBRARY:
			{
				linkCommand = "DLL";
				break;
			}
			case cmTarget::STATIC_LIBRARY:
			case cmTarget::MODULE_LIBRARY:
			case cmTarget::OBJECT_LIBRARY:
			{
				// No changes required
				break;
			}
			case cmTarget::UTILITY:
			case cmTarget::GLOBAL_TARGET:
			{
				// No link command used 
				linkCommand = "NoLinkCommand";
				break;
			}
			case cmTarget::UNKNOWN_LIBRARY:
			{
				// Ignoring this target generation...
				return;
			}
		}

		const std::string& targetName = target.GetName();
		cmGeneratorTarget *gt = context.self->GetGeneratorTarget(&target);

		context.fc.WriteComment("Target definition: "+targetName);
		context.fc.WritePushScope();

		std::vector<std::string> dependencies;
		Detection::DetectTargetCompileDependencies(context.self, target, dependencies);

		// Iterate over each configuration
		// This time to define linker settings for each config
		std::vector<std::string>::const_iterator
				configIter = context.self->GetConfigurations().begin(),
				configEnd = context.self->GetConfigurations().end();
		for (; configIter != configEnd; ++configIter)
		{
			const std::string & configName = *configIter;

			context.fc.WriteVariable("BaseConfig_" + configName, "");
			context.fc.WritePushScopeStruct();

			context.fc.WriteCommand("Using", ".ConfigBase");

			context.fc.WriteVariable("ConfigName", Quote(configName));

			context.fc.WriteBlankLine();
			context.fc.WriteComment("General output details:");
			// Write out the output paths for the outcome of this target
			{
				Detection::FastbuildTargetNames targetNames;
				Detection::DetectOutput(targetNames, target, configName);

				context.fc.WriteVariable("TargetNameOut", Quote(targetNames.targetNameOut));
				context.fc.WriteVariable("TargetNameImport", Quote(targetNames.targetNameImport));
				context.fc.WriteVariable("TargetNamePDB", Quote(targetNames.targetNamePDB));
				context.fc.WriteVariable("TargetNameSO", Quote(targetNames.targetNameSO));
				context.fc.WriteVariable("TargetNameReal", Quote(targetNames.targetNameReal));

				// TODO: Remove this if these variables aren't used... 
				// They've been added for testing
				context.fc.WriteVariable("TargetOutput", Quote(targetNames.targetOutput));
				context.fc.WriteVariable("TargetOutputImplib", Quote(targetNames.targetOutputImplib));
				context.fc.WriteVariable("TargetOutputReal", Quote(targetNames.targetOutputReal));
				context.fc.WriteVariable("TargetOutDir", Quote(targetNames.targetOutputDir));
				context.fc.WriteVariable("TargetOutCompilePDBDir", Quote(targetNames.targetOutputCompilePDBDir));
				context.fc.WriteVariable("TargetOutPDBDir", Quote(targetNames.targetOutputPDBDir));

				// Compile directory always needs to exist
				EnsureDirectoryExists(targetNames.targetOutputCompilePDBDir, context);

				if (target.GetType() != cmTarget::OBJECT_LIBRARY)
				{
					// on Windows the output dir is already needed at compile time
					// ensure the directory exists (OutDir test)
					EnsureDirectoryExists(targetNames.targetOutputDir, context);
					EnsureDirectoryExists(targetNames.targetOutputPDBDir, context);
				}
			}

			// Write the dependency list in here too
			// So all dependant libraries are built before this one is
			// This is incase this library depends on code generated from previous ones
			{
				context.fc.WriteArray("PreBuildDependencies",
					Wrap(dependencies, "'", "-" + configName + "'"));
			}

			context.fc.WritePopScope();
		}

		// Output the prebuild/Prelink commands
		WriteCustomBuildSteps(context, lg, target, target.GetPreBuildCommands(), "PreBuild", dependencies);
		WriteCustomBuildSteps(context, lg, target, target.GetPreLinkCommands(), "PreLink", dependencies);

		// Iterate over each configuration
		// This time to define prebuild and post build targets for each config
		configIter = context.self->GetConfigurations().begin();
		configEnd = context.self->GetConfigurations().end();
		for (; configIter != configEnd; ++configIter)
		{
			const std::string & configName = *configIter;

			context.fc.WriteVariable("BaseCompilationConfig_" + configName, "");
			context.fc.WritePushScopeStruct();

			context.fc.WriteCommand("Using", ".BaseConfig_" + configName);

			// Add to the list of prebuild deps
			// The prelink and prebuild commands
			{
				std::vector<std::string> preBuildSteps;
				if (!target.GetPreBuildCommands().empty())
				{
					preBuildSteps.push_back("PreBuild");
				}
				if (!target.GetPreLinkCommands().empty())
				{
					preBuildSteps.push_back("PreLink");
				}

				if (!preBuildSteps.empty())
				{
					context.fc.WriteArray("PreBuildDependencies",
						Wrap(preBuildSteps, "'" + targetName + "-", "-" + configName + "'"),
						"+");
				}
			}

			context.fc.WritePopScope();
		}

		// Write the custom build rules
		bool hasCustomBuildRules = WriteCustomBuildRules(context, lg, gt, target);
		
		// Figure out the list of languages in use by this target
		std::vector<std::string> linkableDeps;
		std::vector<std::string> orderDeps;
		std::set<std::string> languages;
		Detection::DetectLanguages(languages, context.self, target);

		// Write the object list definitions for each language
		// stored in this target
		for (std::set<std::string>::iterator langIter = languages.begin();
			langIter != languages.end(); ++langIter)
		{
			const std::string & objectGroupLanguage = *langIter;
			std::string ruleObjectGroupName = "ObjectGroup_" + objectGroupLanguage;
			linkableDeps.push_back(ruleObjectGroupName);

			context.fc.WriteVariable(ruleObjectGroupName, "");
			context.fc.WritePushScopeStruct();

			// Iterating over all configurations
			configIter = context.self->GetConfigurations().begin();
			for (; configIter != configEnd; ++configIter)
			{
				const std::string & configName = *configIter;
				context.fc.WriteVariable("ObjectConfig_" + configName, "");
				context.fc.WritePushScopeStruct();

				context.fc.WriteCommand("Using", ".BaseCompilationConfig_" + configName);
				context.fc.WriteCommand("Using", ".CustomCommands_" + configName);

				context.fc.WriteBlankLine();
				context.fc.WriteComment("Compiler options:");

				// Compiler options
				std::string baseCompileFlags;
				{
					// Remove the command from the front and leave the flags behind
					std::string compileCmd;
					Detection::DetectBaseCompileCommand(compileCmd,
						lg, target, objectGroupLanguage);

					// No need to double unescape the variables
					//Detection::UnescapeFastbuildVariables(compileCmd);

					std::string executable;
					Detection::SplitExecutableAndFlags(compileCmd, executable, baseCompileFlags);

					context.fc.WriteVariable("CompilerCmdBaseFlags", Quote(baseCompileFlags));

					std::string compilerName = ".Compiler_" + objectGroupLanguage;
					context.fc.WriteVariable("Compiler", compilerName);
				}

				std::map<std::string,CompileCommand> commandPermutations;

				// Source files
				context.fc.WriteBlankLine();
				context.fc.WriteComment("Source files:");
				{
					// get a list of source files
					std::vector<cmSourceFile const*> objectSources;
					gt->GetObjectSources(objectSources, configName);

					std::vector<cmSourceFile const*> filteredObjectSources;
					Detection::FilterSourceFiles(filteredObjectSources, objectSources,
						objectGroupLanguage);

					// Figure out the compilation commands for all
					// the translation units in the compilation.
					// Detect if one of them is a PreCompiledHeader
					// and extract it to be used in a precompiled header
					// generation step.
					for (std::vector<cmSourceFile const*>::iterator sourceIter = filteredObjectSources.begin();
						sourceIter != filteredObjectSources.end(); ++sourceIter)
					{
						cmSourceFile const *srcFile = *sourceIter;
						std::string sourceFile = srcFile->GetFullPath();

						// Detect flags and defines
						std::string compilerFlags;
						Detection::DetectCompilerFlags(compilerFlags, 
							lg, gt, target, srcFile, objectGroupLanguage, configName);
						std::string compileDefines = 
							Detection::ComputeDefines(lg, target, srcFile, configName, objectGroupLanguage);
						
						Detection::UnescapeFastbuildVariables(compilerFlags);
						Detection::UnescapeFastbuildVariables(compileDefines);

						std::string configKey = compilerFlags + "{|}" + compileDefines;
						CompileCommand& command = commandPermutations[configKey];
						command.sourceFiles[srcFile->GetLocation().GetDirectory()].push_back(sourceFile);
						command.flags = compilerFlags;
						command.defines = compileDefines;
					}
				}

				// Iterate over all subObjectGroups
				std::string objectGroupRuleName = targetName + "-" + ruleObjectGroupName + "-" + configName;
				std::vector<std::string> configObjectGroups;
				int groupNameCount = 1;
				for (std::map<std::string, CompileCommand>::iterator groupIter = commandPermutations.begin();
					groupIter != commandPermutations.end();
					++groupIter)
				{
					context.fc.WritePushScope();

					const CompileCommand& command = groupIter->second;

					context.fc.WriteVariable("CompileDefineFlags", Quote( command.defines ));
					context.fc.WriteVariable("CompileFlags", Quote( command.flags ));
					context.fc.WriteVariable("CompilerOptions", Quote("$CompileFlags$ $CompileDefineFlags$ $CompilerCmdBaseFlags$"));

					if(objectGroupLanguage == "RC")
					{
						context.fc.WriteVariable("CompilerOutputExtension", Quote( ".res" ));
					}
					else
					{
						context.fc.WriteVariable("CompilerOutputExtension", Quote( "." + objectGroupLanguage + ".obj" ));
					}

					std::map<std::string, std::vector<std::string> >::const_iterator objectListIt;
					for(objectListIt = command.sourceFiles.begin(); objectListIt != command.sourceFiles.end(); ++objectListIt)
					{
						const std::string folderName(Detection::GetLastFolderName(objectListIt->first));
						std::stringstream ruleName;
						ruleName << objectGroupRuleName << "-" << folderName << "-" << (groupNameCount++);

						context.fc.WriteCommand("ObjectList", Quote(ruleName.str()));
						context.fc.WritePushScope();

						context.fc.WriteArray("CompilerInputFiles",
							Wrap(objectListIt->second, "'", "'"));

						configObjectGroups.push_back(ruleName.str());

						std::string targetCompileOutDirectory =
							Detection::DetectTargetCompileOutputDir(lg, target, configName);
						context.fc.WriteVariable("CompilerOutputPath", Quote(targetCompileOutDirectory + "/" + folderName));

						// Unity source files:
						context.fc.WriteVariable("UnityInputFiles", ".CompilerInputFiles");

						/*
						if (Detection::DetectPrecompiledHeader(command.flags + " " +
							baseCompileFlags + " " + command.defines,
							preCompiledHeaderInput,
							preCompiledHeaderOutput,
							preCompiledHeaderOptions)
						*/
						context.fc.WritePopScope();
					}

					context.fc.WritePopScope();
				}

				if(!configObjectGroups.empty()) {
					// Write an alias for this object group to group them all together
					context.fc.WriteCommand("Alias", Quote(objectGroupRuleName));
					context.fc.WritePushScope();
					context.fc.WriteArray("Targets",
						Wrap(configObjectGroups, "'", "'"));
					context.fc.WritePopScope();
				}

				context.fc.WritePopScope();
			}
			context.fc.WritePopScope();
		}

		// Object libraries do not have linker stages
		// nor utilities
		bool hasLinkerStage = 
			target.GetType() != cmTarget::OBJECT_LIBRARY &&
			target.GetType() != cmTarget::UTILITY &&
			target.GetType() != cmTarget::GLOBAL_TARGET;

		// Iterate over each configuration
		// This time to define linker settings for each config
		std::vector<std::string>::const_iterator
				iter = context.self->GetConfigurations().begin(),
				end = context.self->GetConfigurations().end();
		for (; iter != end; ++iter)
		{
			const std::string & configName = *iter;

			std::string linkRuleName = targetName + "-link-" + configName;

			if (hasLinkerStage)
			{

				context.fc.WriteVariable("LinkerConfig_" + configName, "");
				context.fc.WritePushScopeStruct();

				context.fc.WriteCommand("Using", ".BaseConfig_" + configName);

				context.fc.WriteBlankLine();
				context.fc.WriteComment("Linker options:");
				// Linker options
				{
					std::string linkLibs;
					std::string targetFlags;
					std::string linkFlags;
					std::string frameworkPath;
					std::string dummyLinkPath;

					lg->GetTargetFlags(
						linkLibs,
						targetFlags,
						linkFlags,
						frameworkPath,
						dummyLinkPath,
						gt,
						false);

					std::string linkPath;
					Detection::DetectLinkerLibPaths(linkPath, lg, target, configName);

					Detection::UnescapeFastbuildVariables(linkLibs);
					Detection::UnescapeFastbuildVariables(targetFlags);
					Detection::UnescapeFastbuildVariables(linkFlags);
					Detection::UnescapeFastbuildVariables(frameworkPath);
					Detection::UnescapeFastbuildVariables(linkPath);

					linkPath = frameworkPath + linkPath;

					if(target.IsExecutableWithExports())
					{
						const char* defFileFlag = lg->GetMakefile()->GetDefinition("CMAKE_LINK_DEF_FILE_FLAG");
						const std::string defFile = gt->GetModuleDefinitionFile(configName);
						if(!defFile.empty())
						{
							linkFlags += defFileFlag + defFile;
						}
					}

					context.fc.WriteVariable("LinkPath", "'" + linkPath + "'");
					context.fc.WriteVariable("LinkLibs", "'" + linkLibs + "'");
					context.fc.WriteVariable("LinkFlags", "'" + linkFlags + "'");
					context.fc.WriteVariable("TargetFlags", "'" + targetFlags + "'");

					// Remove the command from the front and leave the flags behind
					std::string linkCmd;
					if (!Detection::DetectBaseLinkerCommand(linkCmd,
						lg, target, gt, configName))
					{
						return;
					}
					// No need to do this, because the function above has already escaped things appropriately
					//Detection::UnescapeFastbuildVariables(linkCmd);

					std::string executable;
					std::string flags;
					Detection::SplitExecutableAndFlags(linkCmd, executable, flags);

					std::string language = target.GetLinkerLanguage(configName);
					std::string linkerType = lg->GetMakefile()->GetSafeDefinition("CMAKE_" + language + "_COMPILER_ID");

					context.fc.WriteVariable("Linker", Quote(executable));
					context.fc.WriteVariable("LinkerType", Quote(linkerType));
					context.fc.WriteVariable("BaseLinkerOptions", Quote(flags));

					context.fc.WriteVariable("LinkerOutput", "'$TargetOutput$'");
					context.fc.WriteVariable("LinkerOptions", "'$BaseLinkerOptions$ $LinkLibs$'");

					context.fc.WriteArray("Libraries", 
						Wrap(linkableDeps, "'" + targetName + "-", "-" + configName + "'"));

					// Now detect the extra dependencies for linking
					{
						std::vector<std::string> extraDependencies;
						Detection::DetectTargetObjectDependencies( context.self, target, configName, extraDependencies );
						Detection::DetectTargetLinkDependencies(target, configName, extraDependencies);

						std::for_each(extraDependencies.begin(), extraDependencies.end(), Detection::UnescapeFastbuildVariables);

						context.fc.WriteArray("Libraries", 
							Wrap(extraDependencies, "'", "'"),
							"+");
					}
				
					context.fc.WriteCommand(linkCommand, Quote(linkRuleName));
					context.fc.WritePushScope();
					if (linkCommand == "Library")
					{
						context.fc.WriteComment("Convert the linker options to work with libraries");

						// Push dummy definitions for compilation variables
						// These variables are required by the Library command
						context.fc.WriteVariable("Compiler", ".Compiler_dummy");
						context.fc.WriteVariable("CompilerOptions", "'-c $FB_INPUT_1_PLACEHOLDER$ $FB_INPUT_2_PLACEHOLDER$'");
						context.fc.WriteVariable("CompilerOutputPath", "'/dummy/'");
					
						// These variables are required by the Library command as well
						// we just need to transfer the values in the linker variables
						// to these locations
						context.fc.WriteVariable("Librarian","'$Linker$'");
						context.fc.WriteVariable("LibrarianOptions","'$LinkerOptions$'");
						context.fc.WriteVariable("LibrarianOutput","'$LinkerOutput$'");

						context.fc.WriteVariable("LibrarianAdditionalInputs", ".Libraries");
					}
					context.fc.WritePopScope();
				}
				context.fc.WritePopScope();
			}
		}

		if (!target.GetPreBuildCommands().empty())
		{
			orderDeps.push_back("PreBuild");
		}
		if (!target.GetPreLinkCommands().empty())
		{
			orderDeps.push_back("PreLink");
		}
		if (hasCustomBuildRules)
		{
			orderDeps.push_back("CustomCommands");
		}
		if (hasLinkerStage)
		{
			linkableDeps.push_back("link");
			orderDeps.push_back("link");
		}

		// Output the postbuild commands
		WriteCustomBuildSteps(context, lg, target, target.GetPostBuildCommands(), "PostBuild", 
			Wrap(orderDeps, targetName + "-", ""));

		// Always add the pre/post build steps as
		// part of the alias.
		// This way, if there are ONLY build steps, then
		// things should still work too.
		if (!target.GetPostBuildCommands().empty())
		{
			orderDeps.push_back("PostBuild");
		}

		// Output a list of aliases
		WriteTargetAliases(context, target, linkableDeps, orderDeps);

		context.fc.WritePopScope();
	}

	static void WriteTargetAliases(
		GenerationContext& context,
		cmTarget& target,
		const std::vector<std::string>& linkableDeps,
		const std::vector<std::string>& orderDeps)
	{
		const std::string& targetName = target.GetName();
		
		std::vector<std::string>::const_iterator
            iter = context.self->GetConfigurations().begin(),
			end = context.self->GetConfigurations().end();
		for (; iter != end; ++iter)
		{
			const std::string & configName = *iter;

			if (!linkableDeps.empty())
			{
				context.fc.WriteCommand("Alias",
					Quote(targetName + "-" + configName + "-products"));
				context.fc.WritePushScope();
				context.fc.WriteArray("Targets",
					Wrap(linkableDeps, "'" + targetName + "-", "-" + configName + "'"));
				context.fc.WritePopScope();
			}

			if (!orderDeps.empty() || !linkableDeps.empty())
			{
				context.fc.WriteCommand("Alias",
					Quote(targetName + "-" + configName));
				context.fc.WritePushScope();
				context.fc.WriteArray("Targets",
					Wrap(linkableDeps, "'" + targetName + "-", "-" + configName + "'"));
				context.fc.WriteArray("Targets",
					Wrap(orderDeps, "'" + targetName + "-", "-" + configName + "'"),
					"+");
				context.fc.WritePopScope();
			}
		}
	}

	static void WriteTargetUtilityDefinition(GenerationContext& context,
		cmLocalFastbuildGenerator *lg, cmTarget &target)
	{
		WriteTargetDefinition(context, lg, target);
	}

	static void WriteTargetDefinitions(GenerationContext& context,
		bool outputGlobals)
	{
		context.fc.WriteSectionHeader((outputGlobals)?"Global Target Definitions":"Target Definitions");

		// Now iterate each target in order
		for (OrderedTargets::iterator targetIter = context.orderedTargets.begin(); 
			targetIter != context.orderedTargets.end();
			++targetIter)
		{
			const cmTarget* constTarget = (*targetIter);
			if(constTarget->GetType() == cmTarget::INTERFACE_LIBRARY)
			{
				continue;
			}

			TargetContextMap::iterator findResult = context.targetContexts.find(constTarget);
			if (findResult == context.targetContexts.end())
			{
				continue;
			}

			cmTarget* target = findResult->second.target;
			cmLocalFastbuildGenerator* lg = findResult->second.lg;

			if (target->GetType() == cmTarget::GLOBAL_TARGET)
			{
				if (!outputGlobals)
					continue;
			}
			else
			{
				if (outputGlobals)
					continue;
			}

			switch (target->GetType())
			{
				case cmTarget::EXECUTABLE:
				case cmTarget::SHARED_LIBRARY:
				case cmTarget::STATIC_LIBRARY:
				case cmTarget::MODULE_LIBRARY:
				case cmTarget::OBJECT_LIBRARY:
					WriteTargetDefinition(context, lg, *target);
					break;
				case cmTarget::UTILITY:
				case cmTarget::GLOBAL_TARGET:
					WriteTargetUtilityDefinition(context, lg, *target);
					break;
				default:
					break;
			}
		}
	}
	
	static void WriteAliases(GenerationContext& context,
		bool outputGlobals)
	{
		context.fc.WriteSectionHeader("Aliases");

		// Write the following aliases:
		// Per Target
		// Per Config
		// All

		typedef std::map<std::string, std::vector<std::string> > TargetListMap;
		TargetListMap perConfig;
		TargetListMap perTarget;

		for (OrderedTargets::iterator targetIter = context.orderedTargets.begin(); 
			targetIter != context.orderedTargets.end();
			++targetIter)
		{
			const cmTarget* constTarget = (*targetIter);
			if(constTarget->GetType() == cmTarget::INTERFACE_LIBRARY)
			{
				continue;
			}

			TargetContextMap::iterator findResult = context.targetContexts.find(constTarget);
			if (findResult == context.targetContexts.end())
			{
				continue;
			}

			cmTarget* target = findResult->second.target;
			const std::string & targetName = target->GetName();

			if (target->GetType() == cmTarget::GLOBAL_TARGET)
			{
				if (!outputGlobals)
					continue;
			}
			else
			{
				if (outputGlobals)
					continue;
			}

			// Define compile flags
			std::vector<std::string>::const_iterator
				iter = context.self->GetConfigurations().begin(),
				end = context.self->GetConfigurations().end();
			for (; iter != end; ++iter)
			{
				const std::string & configName = *iter;
				std::string aliasName = targetName + "-" + configName;

				perTarget[targetName].push_back(aliasName);

				if (!Detection::IsExcludedFromAll(target))
				{
					perConfig[configName].push_back(aliasName);
				}
			}
		}

		if (!outputGlobals)
		{
			context.fc.WriteComment("Per config");
			std::vector<std::string> aliasTargets;
			for (TargetListMap::iterator iter = perConfig.begin();
				iter != perConfig.end(); ++iter)
			{
				const std::string & configName = iter->first;
				const std::vector<std::string> & targets = iter->second;

				context.fc.WriteCommand("Alias", Quote(configName));
				context.fc.WritePushScope();
				context.fc.WriteArray("Targets", 
					Wrap(targets, "'", "'"));
				context.fc.WritePopScope();

				aliasTargets.clear();
				aliasTargets.push_back(configName);
				context.fc.WriteCommand("Alias", Quote("ALL_BUILD-" + configName));
				context.fc.WritePushScope();
				context.fc.WriteArray("Targets", 
					Wrap(aliasTargets, "'", "'"));
				context.fc.WritePopScope();
			}
		}

		context.fc.WriteComment("Per targets");
		for (TargetListMap::iterator iter = perTarget.begin();
			iter != perTarget.end(); ++iter)
		{
			const std::string & targetName = iter->first;
			const std::vector<std::string> & targets = iter->second;

			context.fc.WriteCommand("Alias", "'" + targetName + "'");
			context.fc.WritePushScope();
			context.fc.WriteArray("Targets", 
				Wrap(targets, "'", "'"));
			context.fc.WritePopScope();
		}

		if (!outputGlobals)
		{
			context.fc.WriteComment("All");
			context.fc.WriteCommand("Alias", "'All'");
			context.fc.WritePushScope();
			context.fc.WriteArray("Targets", 
				Wrap(context.self->GetConfigurations(), "'", "'"));
			context.fc.WritePopScope();
		}
	}
};

//----------------------------------------------------------------------------
cmGlobalGeneratorFactory* cmGlobalFastbuildGenerator::NewFactory()
{
	return new Factory();
}

//----------------------------------------------------------------------------
cmGlobalFastbuildGenerator::cmGlobalFastbuildGenerator()
{
	this->FindMakeProgramFile = "CMakeFastbuildFindMake.cmake";
}

//----------------------------------------------------------------------------
cmGlobalFastbuildGenerator::~cmGlobalFastbuildGenerator()
{

}

//----------------------------------------------------------------------------
std::string cmGlobalFastbuildGenerator::GetName() const 
{
	return fastbuildGeneratorName; 
}

//----------------------------------------------------------------------------
cmLocalGenerator *cmGlobalFastbuildGenerator::CreateLocalGenerator()
{
	cmLocalGenerator * lg = new cmLocalFastbuildGenerator();
	lg->SetGlobalGenerator(this);
	return lg;
}

//----------------------------------------------------------------------------
void cmGlobalFastbuildGenerator::EnableLanguage(
	std::vector<std::string>const &  lang,
	cmMakefile *mf, bool optional)
{
	// Create list of configurations requested by user's cache, if any.
	this->cmGlobalGenerator::EnableLanguage(lang, mf, optional);
	Detail::Detection::DetectConfigurations(this, mf, this->Configurations);
}

//----------------------------------------------------------------------------
void cmGlobalFastbuildGenerator::Generate()
{
	// Execute the standard generate process
	cmGlobalGenerator::Generate();

	Detail::Generation::GenerateRootBFF(this);
}

//----------------------------------------------------------------------------
void cmGlobalFastbuildGenerator::GenerateBuildCommand(
	std::vector<std::string>& makeCommand,
	const std::string& makeProgram,
	const std::string& projectName,
	const std::string& projectDir,
	const std::string& targetName,
	const std::string& config,
	bool /*fast*/, bool /*verbose*/,
	std::vector<std::string> const& /*makeOptions*/)
{
	// A build command for fastbuild looks like this:
	// fbuild.exe [make-options] [-config projectName.bff] <target>-<config>

	// Setup make options
	std::vector<std::string> makeOptionsSelected;

	// Select the caller- or user-preferred make program
	std::string makeProgramSelected =
		this->SelectMakeProgram(makeProgram);

	// Select the target
	std::string targetSelected = targetName;
	// Note an empty target is a valid target (defaults to ALL anyway)
	if (targetSelected == "clean")
	{
		makeOptionsSelected.push_back("-clean");
		targetSelected = "";
	}
  
	// Select the config
	std::string configSelected = config;
	if (configSelected.empty())
	{
		configSelected = "Debug";
	}

	// Select fastbuild target
	if (targetSelected.empty())
	{
		targetSelected = configSelected;
	}
	else
	{
		targetSelected += "-" + configSelected;
	}

	// Hunt the fbuild.bff file in the directory above
	std::string configFile;
	if (!cmSystemTools::FileExists(projectDir + "fbuild.bff"))
	{
		configFile = cmSystemTools::FileExistsInParentDirectories("fbuild.bff", projectDir.c_str(), "");
	}

	// Build the command
	makeCommand.push_back(makeProgramSelected);
	
	// Push in the make options
	makeCommand.insert(makeCommand.end(), makeOptionsSelected.begin(), makeOptionsSelected.end());

	/*
	makeCommand.push_back("-config");
	makeCommand.push_back(projectName + ".bff");
	*/

	makeCommand.push_back("-showcmds");
	makeCommand.push_back("-ide");

	if (!configFile.empty())
	{
		makeCommand.push_back("-config");
		makeCommand.push_back(configFile);
	}

	// Add the target-config to the command
	if (!targetSelected.empty())
	{
		makeCommand.push_back(targetSelected);
	}
}

//----------------------------------------------------------------------------
void cmGlobalFastbuildGenerator::AppendDirectoryForConfig(
	const std::string& prefix,
	const std::string& config,
	const std::string& suffix,
	std::string& dir)
{
	if(!config.empty())
	{
		dir += prefix;
		dir += config;
		dir += suffix;
	}
}

//----------------------------------------------------------------------------
void cmGlobalFastbuildGenerator::ComputeTargetObjectDirectory(
	cmGeneratorTarget* gt) const
{
	cmTarget* target = gt->Target;

	// Compute full path to object file directory for this target.
	std::string dir;
	dir += gt->Makefile->GetCurrentOutputDirectory();
	dir += "/";
	dir += gt->LocalGenerator->GetTargetDirectory(*target);
	dir += "/";
	gt->ObjectDirectory = dir;
}

//----------------------------------------------------------------------------
const char* cmGlobalFastbuildGenerator::GetCMakeCFGIntDir() const
{
	return FASTBUILD_DOLLAR_TAG "ConfigName" FASTBUILD_DOLLAR_TAG;
}

//----------------------------------------------------------------------------
void cmGlobalFastbuildGenerator::GetTargetSets(TargetDependSet& projectTargets,
					TargetDependSet& originalTargets,
					cmLocalGenerator* root, GeneratorVector const& gv)
{
	cmGlobalGenerator::GetTargetSets(projectTargets, originalTargets, root, gv);
}
//----------------------------------------------------------------------------
const std::vector<std::string> & cmGlobalFastbuildGenerator::GetConfigurations() const
{
	return Configurations;
}

//----------------------------------------------------------------------------
