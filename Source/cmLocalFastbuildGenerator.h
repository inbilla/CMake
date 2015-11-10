/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#ifndef cmLocalFastbuildGenerator_h
#define cmLocalFastbuildGenerator_h

#include "cmLocalGenerator.h"

#include <cmsys/auto_ptr.hxx>

class cmSourceFile;
class cmSourceGroup;
class cmCustomCommand;
class cmCustomCommandGenerator;

/** \class cmLocalFastbuildGenerator
 * \brief Base class for Visual Studio generators.
 *
 * cmLocalFastbuildGenerator provides functionality common to all
 * Visual Studio generators.
 */
class cmLocalFastbuildGenerator 
	: public cmLocalGenerator
{
public:
  
	cmLocalFastbuildGenerator();
	virtual ~cmLocalFastbuildGenerator();

	virtual void Generate();

	void ExpandRuleVariables(std::string& s, const RuleVariables& replaceValues);
	virtual std::string ConvertToLinkReference(
		std::string const& lib,
		OutputFormat format);
	virtual void ComputeObjectFilenames(
		std::map<cmSourceFile const*, std::string>& mapping,
		cmGeneratorTarget const* gt);
	virtual std::string GetTargetDirectory(
		cmTarget const& target) const;
	virtual void AppendFlagEscape(std::string& flags,
		const std::string& rawFlag);

};

#endif
