#include "stdafx.h"

#include "Headers\Globals.h"

#include "Headers\Chip8Engine\Chip8Engine_Timers.h"

std::string Chip8Engine_Timers::getComponentName()
{
	return std::string("Timers");
}

Chip8Engine_Timers::Chip8Engine_Timers()
{
	// Register this component in logger
	logger->registerComponent(this);
}

Chip8Engine_Timers::~Chip8Engine_Timers()
{
	// Deregister this component in logger
	logger->deregisterComponent(this);
}