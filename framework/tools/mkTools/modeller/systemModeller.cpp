//--------------------------------------------------------------------------------------------------
/**
 * @file systemModeller.cpp
 *
 * Copyright (C) Sierra Wireless Inc.  Use of this work is subject to license.
 */
//--------------------------------------------------------------------------------------------------

#include "mkTools.h"
#include "modellerCommon.h"


namespace modeller
{


//--------------------------------------------------------------------------------------------------
/**
 * Get environment variable settings from a "buildVars:" section and set them in the process
 * environment.
 */
//--------------------------------------------------------------------------------------------------
static void GetBuildEnvVars
(
    const parseTree::CompoundItem_t* sectionPtr
)
//--------------------------------------------------------------------------------------------------
{
    auto buildVarsSectionPtr = dynamic_cast<const parseTree::CompoundItemList_t*>(sectionPtr);

    for (const auto contentItemPtr : buildVarsSectionPtr->Contents())
    {
        auto buildVarPtr = dynamic_cast<const parseTree::EnvVar_t*>(contentItemPtr);

        // Make sure they're not trying to redefine one of the reserved environment variables
        // like LEGATO_ROOT.
        const auto& name = buildVarPtr->firstTokenPtr->text;
        if (envVars::IsReserved(name))
        {
            buildVarPtr->firstTokenPtr->ThrowException(name
                                                     + " is a reserved environment variable name.");
        }

        // If the string starts with a single quote ('), just unquote it.  Otherwise, unquote it
        // and do environment variable substitution.
        auto valueTokenPtr = buildVarPtr->Contents()[0];
        std::string value = path::Unquote(valueTokenPtr->text);
        if (valueTokenPtr->text[0] != '\'')
        {
            value = envVars::DoSubstitution(value);
        }

        // Update the process environment.
        if (setenv(name.c_str(), value.c_str(), true /* overwrite existing */) != 0)
        {
            throw mk::Exception_t("Failed to set '" + name + "' environment variable to '"
                                   + value + "'.");
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Updates an App_t object with the overrides specified for that app in the .sdef file.
 */
//--------------------------------------------------------------------------------------------------
static void ModelAppOverrides
(
    model::App_t* appPtr,
    const parseTree::App_t* appSectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    bool groupsOverridden = false;

    // Iterate over the list of contents of the app section in the parse tree.
    for (auto subsectionPtr : appSectionPtr->Contents())
    {
        auto& subsectionName = subsectionPtr->firstTokenPtr->text;

        if (subsectionName == "cpuShare")
        {
            appPtr->cpuShare = GetPositiveInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "faultAction")
        {
            for (auto procEnvPtr : appPtr->processEnvs)
            {
                procEnvPtr->faultAction = ToSimpleSectionPtr(subsectionPtr)->Text();
            }
        }
        else if (subsectionName == "groups")
        {
            if (!groupsOverridden)
            {
                appPtr->groups.clear();
                groupsOverridden = true;
            }
            AddGroups(appPtr, ToTokenListSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "maxCoreDumpFileBytes")
        {
            for (auto procEnvPtr : appPtr->processEnvs)
            {
                procEnvPtr->maxCoreDumpFileBytes =
                                               GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
            }
        }
        else if (subsectionName == "maxFileBytes")
        {
            for (auto procEnvPtr : appPtr->processEnvs)
            {
                procEnvPtr->maxFileBytes = GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
            }
        }
        else if (subsectionName == "maxFileDescriptors")
        {
            for (auto procEnvPtr : appPtr->processEnvs)
            {
                procEnvPtr->maxFileDescriptors = GetPositiveInt(ToSimpleSectionPtr(subsectionPtr));
            }
        }
        else if (subsectionName == "maxFileSystemBytes")
        {
            appPtr->maxFileSystemBytes = GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "maxLockedMemoryBytes")
        {
            for (auto procEnvPtr : appPtr->processEnvs)
            {
                procEnvPtr->maxLockedMemoryBytes =
                                               GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
            }
        }
        else if (subsectionName == "maxMemoryBytes")
        {
            appPtr->maxMemoryBytes = GetPositiveInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "maxMQueueBytes")
        {
            appPtr->maxMQueueBytes = GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "maxPriority")
        {
            for (auto procEnvPtr : appPtr->processEnvs)
            {
                procEnvPtr->SetMaxPriority(ToSimpleSectionPtr(subsectionPtr)->Text());
            }
        }
        else if (subsectionName == "maxQueuedSignals")
        {
            appPtr->maxQueuedSignals = GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "maxThreads")
        {
            appPtr->maxThreads = GetPositiveInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "maxSecureStorageBytes")
        {
            appPtr->maxSecureStorageBytes = GetNonNegativeInt(ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "sandboxed")
        {
            appPtr->isSandboxed = (ToSimpleSectionPtr(subsectionPtr)->Text() != "false");
        }
        else if (subsectionName == "start")
        {
            SetStart(appPtr, ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "watchdogAction")
        {
            SetWatchdogAction(appPtr, ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "watchdogTimeout")
        {
            SetWatchdogTimeout(appPtr, ToSimpleSectionPtr(subsectionPtr));
        }
        else if (subsectionName == "preloaded")
        {
            const auto& tokenText = ToSimpleSectionPtr(subsectionPtr)->Text();
            appPtr->isPreloaded = (tokenText != "false");
            if ((tokenText != "true") && (tokenText != "false"))
            {
                appPtr->preloadedMd5 = tokenText;
            }
        }
        else
        {
            subsectionPtr->ThrowException("Internal error: Unexpected subsection "
                                          "'" + subsectionName + "'.");
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates an App_t object for a given app's subsection within an "apps:" section.
 */
//--------------------------------------------------------------------------------------------------
static void ModelApp
(
    model::System_t* systemPtr,
    const parseTree::App_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    std::string appName;
    std::string adefPath;

    // The first token in the app subsection could be the name of an app or a .adef file path.
    // Find the app name and .adef file.
    const auto appSpec = path::Unquote(envVars::DoSubstitution(sectionPtr->firstTokenPtr->text));
    if (path::HasSuffix(appSpec, ".adef"))
    {
        appName = path::RemoveSuffix(path::GetLastNode(appSpec), ".adef");
        adefPath = file::FindFile(appSpec, buildParams.sourceDirs);
    }
    else
    {
        appName = path::GetLastNode(appSpec);
        adefPath = file::FindFile(appSpec + ".adef", buildParams.sourceDirs);
    }

    if (adefPath.empty())
    {
        std::cerr << "Looked in the following places:" << std::endl;
        for (auto& dir : buildParams.sourceDirs)
        {
            std::cerr << "  '" << dir << "'" << std::endl;
        }
        sectionPtr->ThrowException("Can't find definition file (.adef) for app specification "
                                   "'" + appSpec + "'.");
    }

    // Check for duplicates.
    auto appsIter = systemPtr->apps.find(appName);
    if (appsIter != systemPtr->apps.end())
    {
        std::stringstream msg;
        msg << "App '" << appName << "' added to the system more than once.  Previously added at"
            "line " << appsIter->second->parseTreePtr->firstTokenPtr->line << ".";
        sectionPtr->ThrowException(msg.str());
    }

    if (buildParams.beVerbose)
    {
        std::cout << "System contains app '" << appName << "'." << std::endl;
    }

    // Model this app.
    auto appPtr = GetApp(adefPath, buildParams);
    appPtr->parseTreePtr = sectionPtr;

    systemPtr->apps[appName] = appPtr;

    // Now apply any overrides specified in the .sdef file.
    ModelAppOverrides(appPtr, sectionPtr, buildParams);
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates an App_t object for each app listed in an "apps:" section.
 */
//--------------------------------------------------------------------------------------------------
static void ModelAppsSection
(
    model::System_t* systemPtr,
    const parseTree::CompoundItem_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto appsSectionPtr = dynamic_cast<const parseTree::CompoundItemList_t*>(sectionPtr);

    for (auto itemPtr : appsSectionPtr->Contents())
    {
        ModelApp(systemPtr, dynamic_cast<const parseTree::App_t*>(itemPtr), buildParams);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a Module_t object for a given kernel module within "kernelModules:" section.
 */
//--------------------------------------------------------------------------------------------------
static void ModelKernelModule
(
    model::System_t* systemPtr,
    const parseTree::Module_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    std::string moduleName;
    std::string modulePath;

    // Tokens in the module subsection are paths to their .mdef file
    // Assume that modules are built outside of Legato
    const auto moduleSpec = path::Unquote(envVars::DoSubstitution(sectionPtr->firstTokenPtr->text));
    if (path::HasSuffix(moduleSpec, ".mdef"))
    {
        moduleName = path::RemoveSuffix(path::GetLastNode(moduleSpec), ".mdef");
        modulePath = file::FindFile(moduleSpec, buildParams.sourceDirs);
    }
    else
    {
        // Try by appending ".mdef" to path
        moduleName = path::GetLastNode(moduleSpec);
        modulePath = file::FindFile(moduleSpec + ".mdef", buildParams.sourceDirs);
    }

    if (modulePath.empty())
    {
        std::cerr << "Looked in the following places:" << std::endl;
        for (auto& dir : buildParams.sourceDirs)
        {
            std::cerr << "  '" << dir << "'" << std::endl;
        }
        sectionPtr->ThrowException("Can't find definition (.mdef) file for module specification "
                                   "'" + moduleSpec + "'.");
    }

    // Check for duplicates.
    auto modulesIter = systemPtr->modules.find(moduleName);
    if (modulesIter != systemPtr->modules.end())
    {
        std::stringstream msg;
        msg << "Module '" << moduleName << "' added to the system more than once.  Previously added at"
            "line " << modulesIter->second->parseTreePtr->firstTokenPtr->line << ".";
        sectionPtr->ThrowException(msg.str());
    }

    // Model this module.
    auto modulePtr = GetModule(modulePath, buildParams);
    modulePtr->parseTreePtr = sectionPtr;

    systemPtr->modules[moduleName] = modulePtr;

    if (buildParams.beVerbose)
    {
        std::cout << "System contains module '" << moduleName << "'." << std::endl;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a Module_t object for each kernel module listed in "kernelModules:" section.
 */
//--------------------------------------------------------------------------------------------------
static void ModelKernelModulesSection
(
    model::System_t* systemPtr,
    const parseTree::CompoundItem_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto moduleSectionPtr = dynamic_cast<const parseTree::CompoundItemList_t*>(sectionPtr);

    for (auto itemPtr : moduleSectionPtr->Contents())
    {
        ModelKernelModule(systemPtr,
                          dynamic_cast<const parseTree::Module_t*>(itemPtr),
                          buildParams);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Model all the kernel modules from all the "kernelModules:" sections and add them to a system.
 */
//--------------------------------------------------------------------------------------------------
static void ModelKernelModules
(
    model::System_t* systemPtr,
    const std::list<const parseTree::CompoundItem_t*>& kernelModulesSections,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    for (auto sectionPtr : kernelModulesSections)
    {
        ModelKernelModulesSection(systemPtr, sectionPtr, buildParams);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Exract the server side details from a bindings section in the parse tree.
 */
//--------------------------------------------------------------------------------------------------
static void GetBindingServerSide
(
    model::Binding_t* bindingPtr,   ///< Binding object to populate with the server-side details.
    const parseTree::Token_t* agentTokenPtr,
    const parseTree::Token_t* interfaceTokenPtr,
    model::System_t* systemPtr
)
//--------------------------------------------------------------------------------------------------
{
    const auto& agentName = agentTokenPtr->text;
    const auto& interfaceName = interfaceTokenPtr->text;

    // Set the server interface name.
    bindingPtr->serverIfName = interfaceName;

    // Set the server agent type and name.
    if (agentName[0] == '<')  // non-app user?
    {
        bindingPtr->serverType = model::Binding_t::EXTERNAL_USER;
        bindingPtr->serverAgentName = RemoveAngleBrackets(agentName);
    }
    else // app
    {
        bindingPtr->serverType = model::Binding_t::EXTERNAL_APP;
        bindingPtr->serverAgentName = agentName;

        // Make sure that the server interface actually exists on an app in the system.
        // (This will throw an exception if it doesn't. We don't need the results returned.)
        systemPtr->FindServerInterface(agentTokenPtr, interfaceTokenPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add a binding to a non-app user's list of bindings.
 */
//--------------------------------------------------------------------------------------------------
static void AddNonAppUserBinding
(
    model::System_t* systemPtr,
    model::Binding_t* bindingPtr
)
//--------------------------------------------------------------------------------------------------
{
    auto& userName = bindingPtr->clientAgentName;
    auto& interfaceName = bindingPtr->clientIfName;
    model::User_t* userPtr = NULL;

    // Get a pointer to the user object, creating a new one if one doesn't exist for this user yet.
    auto userIter = systemPtr->users.find(userName);
    if (userIter == systemPtr->users.end())
    {
        userPtr = new model::User_t(userName);
        systemPtr->users[userName] = userPtr;
    }
    else
    {
        userPtr = userIter->second;
    }

    // Check to make sure this interface is not already bound to something.
    auto i = userPtr->bindings.find(interfaceName);
    if (i != userPtr->bindings.end())
    {
        std::stringstream msg;

        msg << "Duplicate binding of client-side interface '" << interfaceName
            << "' belonging to non-app user '" + userName + "'. Previous binding was at"
               " line " << i->second->parseTreePtr->firstTokenPtr->line << ".";

        bindingPtr->parseTreePtr->ThrowException(msg.str());
    }

    // Add the binding to the user.
    userPtr->bindings[interfaceName] = bindingPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Add all the IPC bindings from a single bindings section to a given system object.
 */
//--------------------------------------------------------------------------------------------------
static void ModelBindingsSection
(
    model::System_t* systemPtr,
    const parseTree::CompoundItem_t* bindingsSectionPtr,
    bool beVerbose
)
//--------------------------------------------------------------------------------------------------
{
    // The bindings section is a list of compound items.
    auto sectionPtr = dynamic_cast<const parseTree::CompoundItemList_t*>(bindingsSectionPtr);

    for (auto itemPtr : sectionPtr->Contents())
    {
        // Each binding specification inside the bindings section is a token list.
        auto bindingSpecPtr = dynamic_cast<const parseTree::Binding_t*>(itemPtr);
        auto tokens = bindingSpecPtr->Contents();

        // Create a new binding object to hold the contents of this binding.
        auto bindingPtr = new model::Binding_t(bindingSpecPtr);

        // The first token is an IPC_AGENT token, which is either an app name or a user name.
        // If it begins with a '<', then it's a user name.  Otherwise, it's an app name.
        if (tokens[0]->text[0] == '<')  // Client is a non-app user.
        {
            // 0         1    2         3
            // IPC_AGENT NAME IPC_AGENT NAME
            auto userName = RemoveAngleBrackets(tokens[0]->text);
            bindingPtr->clientType = model::Binding_t::EXTERNAL_USER;
            bindingPtr->clientAgentName = userName;
            bindingPtr->clientIfName = tokens[1]->text;
            GetBindingServerSide(bindingPtr, tokens[2], tokens[3], systemPtr);

            // Record the binding in the user's list of bindings.
            AddNonAppUserBinding(systemPtr, bindingPtr);
        }
        else // Client is an app.
        {
            // Find the client app.
            auto appPtr = systemPtr->FindApp(tokens[0]);

            // There are three different forms of client interface specifications:
            // - app.interface = set/override an external interface binding.
            // - app.exe.comp.interface = override an internal interface binding.
            // - app.*.interface = override an internal wildcard binding.
            if (tokens[1]->type == parseTree::Token_t::STAR) // wildcard binding
            {
                // 0         1    2    3         4
                // IPC_AGENT STAR NAME IPC_AGENT NAME
                bindingPtr->clientType = model::Binding_t::INTERNAL;
                bindingPtr->clientAgentName = appPtr->name;
                bindingPtr->clientIfName = tokens[2]->text;
                GetBindingServerSide(bindingPtr, tokens[3], tokens[4], systemPtr);

                if (beVerbose)
                {
                    if (   appPtr->wildcardBindings.find(bindingPtr->clientIfName)
                        != appPtr->wildcardBindings.end())
                    {
                        std::cout << "Replacing previous wildcard binding '"
                                  << bindingPtr->clientAgentName << ".*."
                                  << bindingPtr->clientIfName << "'." << std::endl;
                    }
                }

                // Update the list of wildcard bindings.
                appPtr->wildcardBindings[bindingPtr->clientIfName] = bindingPtr;
            }
            else if (tokens.size() == 4) // external interface binding
            {
                // 0         1    2         3
                // IPC_AGENT NAME IPC_AGENT NAME
                auto clientIfPtr = appPtr->FindClientInterface(tokens[1]);
                bindingPtr->clientType = model::Binding_t::EXTERNAL_APP;
                bindingPtr->clientAgentName = appPtr->name;
                bindingPtr->clientIfName = clientIfPtr->name;
                GetBindingServerSide(bindingPtr, tokens[2], tokens[3], systemPtr);

                if (beVerbose)
                {
                    if (clientIfPtr->bindingPtr != NULL)
                    {
                        std::cout << "Overriding binding of '"
                                  << bindingPtr->clientAgentName << "."
                                  << bindingPtr->clientIfName << "'." << std::endl;
                    }
                }

                // Record the binding in the client-side interface object.
                clientIfPtr->bindingPtr = bindingPtr;
            }
            else // internal interface override
            {
                // 0         1    2    3    4         5
                // IPC_AGENT NAME NAME NAME IPC_AGENT NAME
                auto clientIfPtr = appPtr->FindClientInterface(tokens[1], tokens[2], tokens[3]);
                bindingPtr->clientType = model::Binding_t::INTERNAL;
                bindingPtr->clientAgentName = appPtr->name;
                bindingPtr->clientIfName = clientIfPtr->name;
                GetBindingServerSide(bindingPtr, tokens[4], tokens[5], systemPtr);

                if (beVerbose)
                {
                    if (clientIfPtr->bindingPtr != NULL)
                    {
                        std::cout << "Overriding binding of '"
                                  << bindingPtr->clientAgentName << "."
                                  << bindingPtr->clientIfName << "'." << std::endl;
                    }
                }

                // Record the binding in the client-side interface object.
                clientIfPtr->bindingPtr = bindingPtr;
            }
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Model all the apps from all the "apps:" sections and add them to a system.
 */
//--------------------------------------------------------------------------------------------------
static void ModelApps
(
    model::System_t* systemPtr,
    const std::list<const parseTree::CompoundItem_t*>& appsSections,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    for (auto sectionPtr : appsSections)
    {
        ModelAppsSection(systemPtr, sectionPtr, buildParams);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add all the IPC bindings from a list of bindings sections to a given system object.
 */
//--------------------------------------------------------------------------------------------------
static void ModelBindings
(
    model::System_t* systemPtr,
    const std::list<const parseTree::CompoundItem_t*>& bindingsSections,
    bool beVerbose
)
//--------------------------------------------------------------------------------------------------
{
    for (auto bindingsSectionPtr : bindingsSections)
    {
        ModelBindingsSection(systemPtr, bindingsSectionPtr, beVerbose);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add all the commands from a single commands section to a given system.
 */
//--------------------------------------------------------------------------------------------------
static void ModelCommandsSection
(
    model::System_t* systemPtr,
    const parseTree::CompoundItem_t* commandsSectionPtr
)
//--------------------------------------------------------------------------------------------------
{
    // The commands section is a list of compound items.
    auto sectionPtr = dynamic_cast<const parseTree::CompoundItemList_t*>(commandsSectionPtr);

    for (auto itemPtr : sectionPtr->Contents())
    {
        // Each command specification inside the section is a token list.
        auto commandSpecPtr = dynamic_cast<const parseTree::Command_t*>(itemPtr);
        auto tokens = commandSpecPtr->Contents();

        // Create a new command object.
        auto commandPtr = new model::Command_t(commandSpecPtr);

        // The first token is the command name.
        commandPtr->name = path::Unquote(envVars::DoSubstitution(tokens[0]->text));

        // Check for duplicates.
        auto commandIter = systemPtr->commands.find(commandPtr->name);
        if (commandIter != systemPtr->commands.end())
        {
            std::stringstream msg;
            msg << "Command name '" << commandPtr->name << "' used more than once. Previously used"
                   " at line " << commandIter->second->parseTreePtr->firstTokenPtr->line << ".";
            tokens[0]->ThrowException(msg.str());
        }

        // The second token is the app name.
        commandPtr->appPtr = systemPtr->FindApp(tokens[1]);

        // The third token is the path to the executable within the read-only section.
        commandPtr->exePath = tokens[2]->text;

        // Make sure the path is absolute.
        if (!path::IsAbsolute(commandPtr->exePath))
        {
            tokens[2]->ThrowException("Command executable path inside app must begin with '/'.");
        }

        // NOTE: It would be nice to check that the exePath points to something that is executable
        // inside the app, but we don't actually know what's going to be in the app until it is
        // built by ninja, because of the way directory bundling is implemented right now.
        // This should be changed eventually so we can provide a better user experience.
        // commandPtr->appPtr->FindExecutable(tokens[2]);

        // Add the command to the system's map.
        systemPtr->commands[commandPtr->name] = commandPtr;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add all the commands from a list of commands sections to a given system object.
 */
//--------------------------------------------------------------------------------------------------
static void ModelCommands
(
    model::System_t* systemPtr,
    const std::list<const parseTree::CompoundItem_t*>& commandsSections
)
//--------------------------------------------------------------------------------------------------
{
    for (auto commandsSectionPtr : commandsSections)
    {
        ModelCommandsSection(systemPtr, commandsSectionPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get interface search directory paths from an "interfaceSearch:" section and add them to the
 * list in the buildParams object.
 */
//--------------------------------------------------------------------------------------------------
static void GetInterfaceSearchDirs
(
    mk::BuildParams_t& buildParams, ///< Object to add interface search dir paths to.
    const parseTree::TokenList_t* sectionPtr
)
//--------------------------------------------------------------------------------------------------
{
    // An interfaceSearch section is a list of FILE_PATH tokens.
    for (const auto contentItemPtr : sectionPtr->Contents())
    {
        auto tokenPtr = dynamic_cast<const parseTree::Token_t*>(contentItemPtr);

        auto dirPath = path::Unquote(envVars::DoSubstitution(tokenPtr->text));

        // If the environment variable substitution resulted in an empty string, just ignore this.
        if (!dirPath.empty())
        {
            buildParams.interfaceDirs.push_back(dirPath);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add all the interface search dir paths from all "interfaceSearch:" sections to a given
 * BuildParams_t object.
 */
//--------------------------------------------------------------------------------------------------
static void GetInterfaceSearchDirs
(
    mk::BuildParams_t& buildParams, ///< Object to add interface search dir paths to.
    const std::list<const parseTree::CompoundItem_t*>& sectionPtrList
)
//--------------------------------------------------------------------------------------------------
{
    for (auto sectionPtr : sectionPtrList)
    {
        // Each interfaceSearch section is a list of FILE_PATH tokens.
        auto tokenListPtr = dynamic_cast<const parseTree::TokenList_t*>(sectionPtr);

        GetInterfaceSearchDirs(buildParams, tokenListPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a conceptual model for a system whose .sdef file can be found at a given path.
 *
 * @return Pointer to the system object.
 */
//--------------------------------------------------------------------------------------------------
model::System_t* GetSystem
(
    const std::string& sdefPath,    ///< Path to the system's .sdef file.
    mk::BuildParams_t& buildParams  ///< [in,out]
)
//--------------------------------------------------------------------------------------------------
{
    // Save the old CURDIR environment variable value and set it to the dir containing this file.
    auto oldDir = envVars::Get("CURDIR");
    envVars::Set("CURDIR", path::GetContainingDir(sdefPath));

    // Parse the .sdef file.
    const auto sdefFilePtr = parser::sdef::Parse(sdefPath, buildParams.beVerbose);

    // Create a new System_t object for this system.
    auto systemPtr = new model::System_t(sdefFilePtr);

    if (buildParams.beVerbose)
    {
        std::cout << "Modelling system: '" << systemPtr->name << "'" << std::endl
                  << "  defined in: '" << sdefFilePtr->path << "'" << std::endl;
    }

    // Lists of things that need to be modelled near the end.
    std::list<const parseTree::CompoundItem_t*> appsSections;
    std::list<const parseTree::CompoundItem_t*> bindingsSections;
    std::list<const parseTree::CompoundItem_t*> commandsSections;
    std::list<const parseTree::CompoundItem_t*> kernelModulesSections;
    std::list<const parseTree::CompoundItem_t*> interfaceSearchSections;

    // Iterate over the .sdef file's list of sections, processing content items.
    for (auto sectionPtr : sdefFilePtr->sections)
    {
        auto& sectionName = sectionPtr->firstTokenPtr->text;

        if (sectionName == "apps")
        {
            // Remember for later, when we know all build variables have been added to the
            // environment.
            appsSections.push_back(sectionPtr);
        }
        else if (sectionName == "bindings")
        {
            // Remember for later, when we know all interfaces have been instantiated in all
            // executables.
            bindingsSections.push_back(sectionPtr);
        }
        else if (sectionName == "buildVars")
        {
            // Add each build environment variable to the mksys process's environment.
            GetBuildEnvVars(sectionPtr);
        }
        else if (sectionName == "commands")
        {
            // Remember for later, when we know all apps have been instantiated.
            commandsSections.push_back(sectionPtr);
        }
        else if (sectionName == "kernelModules")
        {
            kernelModulesSections.push_back(sectionPtr);
        }
        else if (sectionName == "interfaceSearch")
        {
            interfaceSearchSections.push_back(sectionPtr);
        }
        else
        {
            sectionPtr->ThrowException("Internal error: Unrecognized section '" + sectionName
                                       + "'.");
        }
    }

    // Process all the "interfaceSearch:" sections.  This must be done after all the build
    // environment variable settings have been parsed.
    GetInterfaceSearchDirs(buildParams, interfaceSearchSections);

    // Process all the "apps:" sections.  This must be done after all the build environment
    // variable settings have been parsed.
    ModelApps(systemPtr, appsSections, buildParams);

    // Process bindings.  This must be done after all the components and executables have been
    // modelled and all the external API interfaces have been processed.
    ModelBindings(systemPtr, bindingsSections, buildParams.beVerbose);

    // Ensure that all client-side interfaces have been bound to something.
    EnsureClientInterfacesBound(systemPtr);

    // Model commands.  This must be done after all apps have been modelled.
    ModelCommands(systemPtr, commandsSections);

    // Model kernel modules.  This must be done after all the build environment variable settings
    // have been parsed.
    ModelKernelModules(systemPtr, kernelModulesSections, buildParams);

    // Restore the previous contents of the CURDIR environment variable.
    envVars::Set("CURDIR", oldDir);

    return systemPtr;
}





} // namespace modeller
