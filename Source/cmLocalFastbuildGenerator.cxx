/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmLocalFastbuildGenerator.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmSystemTools.h"
#include "cmCustomCommandGenerator.h"
#ifdef _WIN32
#include "windows.h"
#endif

//----------------------------------------------------------------------------
cmLocalFastbuildGenerator::cmLocalFastbuildGenerator()
{
#ifdef _WIN32
	this->WindowsShell = true;
#endif
	this->TargetImplib = "$TargetOutputImplib$";
}

//----------------------------------------------------------------------------
cmLocalFastbuildGenerator::~cmLocalFastbuildGenerator()
{

}

//----------------------------------------------------------------------------
void cmLocalFastbuildGenerator::Generate()
{
	// Debug messages
	//std::cout << "======== LOCAL Fastbuild Gen ========\n";
	//GetMakefile()->Print();

	// Now generate information for this generator
}

//----------------------------------------------------------------------------
void cmLocalFastbuildGenerator::ExpandRuleVariables(std::string& s, 
	const RuleVariables& replaceValues)
{
	return cmLocalGenerator::ExpandRuleVariables( s, replaceValues );
}

//----------------------------------------------------------------------------
std::string cmLocalFastbuildGenerator::ConvertToLinkReference(
	std::string const& lib,
    OutputFormat format)
{
	return this->Convert(lib, HOME_OUTPUT, format);
}

//----------------------------------------------------------------------------
void cmLocalFastbuildGenerator::ComputeObjectFilenames(
	std::map<cmSourceFile const*, std::string>& mapping,
	cmGeneratorTarget const* gt)
{
	for (std::map<cmSourceFile const*, std::string>::iterator
		si = mapping.begin(); si != mapping.end(); ++si)
	{
		cmSourceFile const* sf = si->first;
		si->second = this->GetObjectFileNameWithoutTarget(*sf,
			gt->ObjectDirectory);
	}
}

//----------------------------------------------------------------------------
std::string cmLocalFastbuildGenerator::GetTargetDirectory(
	cmTarget const& target) const
{
	std::string dir = cmake::GetCMakeFilesDirectoryPostSlash();
	dir += target.GetName();
#if defined(__VMS)
	dir += "_dir";
#else
	dir += ".dir";
#endif
	return dir;
}

//----------------------------------------------------------------------------
std::string EncodeLiteral(const std::string &lit)
{
	std::string result = lit;
	cmSystemTools::ReplaceString(result, "$", "^$");
	return result;
}

//----------------------------------------------------------------------------
void cmLocalFastbuildGenerator::AppendFlagEscape(std::string& flags,
	const std::string& rawFlag)
{
	std::string escapedFlag = this->EscapeForShell(rawFlag);
	// Other make systems will remove the double $ but
	// fastbuild uses ^$ to escape it. So switch to that.
	cmSystemTools::ReplaceString(escapedFlag, "$$", "^$");
	this->AppendFlags(flags, escapedFlag);
}

//----------------------------------------------------------------------------
