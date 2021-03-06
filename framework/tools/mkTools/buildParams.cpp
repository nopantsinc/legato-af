//--------------------------------------------------------------------------------------------------
/**
 * @file buildParams.cpp  Implementation of Build Params object methods.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "mkTools.h"


namespace mk
{


//--------------------------------------------------------------------------------------------------
/**
 * Constructor
 */
//--------------------------------------------------------------------------------------------------
BuildParams_t::BuildParams_t
(
)
//--------------------------------------------------------------------------------------------------
:   beVerbose(false),
    target("localhost"),
    codeGenOnly(false),
    isStandAloneComp(false),
    argc(0),
    argv(NULL)
//--------------------------------------------------------------------------------------------------
{
    std::string frameworkRootPath = envVars::Get("LEGATO_ROOT");

    interfaceDirs.push_back(path::Combine(frameworkRootPath, "interfaces"));

    interfaceDirs.push_back(path::Combine(frameworkRootPath, "framework/include"));
}


} // namespace mk
