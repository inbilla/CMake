/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#ifndef cmGlobalFastbuildGenerator_h
#define cmGlobalFastbuildGenerator_h

#include "cmGlobalGenerator.h"

class cmGlobalGeneratorFactory;

/** \class cmGlobalFastbuildGenerator
 * \brief Class for global fastbuild generator.
 */
class cmGlobalFastbuildGenerator 
	: public cmGlobalGenerator
{
public:
	cmGlobalFastbuildGenerator();
	virtual ~cmGlobalFastbuildGenerator();

	static cmGlobalGeneratorFactory* NewFactory();

	void EnableLanguage(
		std::vector<std::string>const &  lang,
		cmMakefile *mf, bool optional);
	virtual void Generate();
	virtual void GenerateBuildCommand(
		std::vector<std::string>& makeCommand,
		const std::string& makeProgram,
		const std::string& projectName,
		const std::string& projectDir,
		const std::string& targetName,
		const std::string& config,
		bool fast, bool verbose,
		std::vector<std::string> const& makeOptions);

	///! create the correct local generator
	virtual cmLocalGenerator *CreateLocalGenerator();
	virtual std::string GetName() const;

	virtual bool IsMultiConfig() { return true; }

	virtual void AppendDirectoryForConfig(
		const std::string& prefix,
		const std::string& config,
		const std::string& suffix,
		std::string& dir);

	virtual void ComputeTargetObjectDirectory(cmGeneratorTarget*) const;
	virtual const char* GetCMakeCFGIntDir() const;

	virtual void GetTargetSets(TargetDependSet& projectTargets,
							   TargetDependSet& originalTargets,
							   cmLocalGenerator* root, GeneratorVector const&);

	const std::vector<std::string> & GetConfigurations() const;

private:
	class Factory;
	class Detail;

	std::vector<std::string> Configurations;
};

#endif
