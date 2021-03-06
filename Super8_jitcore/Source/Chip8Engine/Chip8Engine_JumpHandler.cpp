#include "stdafx.h"

#include <cstdint>

#include "Headers\Globals.h"
#include "Headers\FastArrayList\FastArrayList.h"

#include "Headers\Chip8Globals\Chip8Globals.h"
#include "Headers\Chip8Engine\Chip8Engine_JumpHandler.h"
#include "Headers\Chip8Engine\Chip8Engine_CacheHandler.h"

using namespace Chip8Globals;

Chip8Engine_JumpHandler::Chip8Engine_JumpHandler()
{
	jump_list = new FastArrayList<JUMP_ENTRY>(1024);
	jump_fill_list = new FastArrayList<int32_t>(1024);
	cond_jump_list = new FastArrayList<COND_JUMP_ENTRY>(1024);

	// Register this component in logger
	logger->registerComponent(this);
}

Chip8Engine_JumpHandler::~Chip8Engine_JumpHandler()
{
	// Deregister this component in logger
	logger->deregisterComponent(this);

	delete cond_jump_list;
	delete jump_fill_list;
	delete jump_list;
}

std::string Chip8Engine_JumpHandler::getComponentName()
{
	return std::string("JumpHandler");
}

int32_t Chip8Engine_JumpHandler::recordJumpEntry(uint16_t c8_to_)
{
	JUMP_ENTRY entry;
	entry.c8_address_to = c8_to_;
	entry.x86_address_to = NULL; // JMP_M_PTR_32 uses this value (unknown at beginning)
	jump_list->push_back(entry);
#ifdef USE_VERBOSE
	char buffer[1000];
	sprintf_s(buffer, 1000, "Jump[%d] recorded. C8_to = 0x%.4X.", jump_list->size() - 1, c8_to_);
	logMessage(LOGLEVEL::L_INFO, buffer);
#endif
	jump_fill_list->push_back(jump_list->size() - 1);
	return (jump_list->size() - 1);
}

int32_t Chip8Engine_JumpHandler::recordConditionalJumpEntry(uint16_t c8_from_, uint16_t c8_to_, uint8_t translator_cycles_, uint32_t * x86_address_jump_value_)
{
	COND_JUMP_ENTRY entry;
	entry.c8_address_from = c8_from_;
	entry.c8_address_to = c8_to_;
	entry.x86_address_jump_value = x86_address_jump_value_;
	entry.translator_cycles = translator_cycles_;
	cond_jump_list->push_back(entry);
#ifdef USE_VERBOSE
	char buffer[1000];
	sprintf_s(buffer, 1000, "Conditional Jump[%d] recorded. C8_from = 0x%.4X, C8_to = 0x%.4X, x86_address = 0x%.8X, cycles = %d.", cond_jump_list->size() - 1, c8_from_, c8_to_, (uint32_t)x86_address_jump_value_, translator_cycles_);
	logMessage(LOGLEVEL::L_INFO, buffer);
#endif
	return (cond_jump_list->size() - 1);
}

void Chip8Engine_JumpHandler::decreaseConditionalCycle()
{
	for (int32_t i = 0; i < (int32_t)cond_jump_list->size(); i++) {
		if (cond_jump_list->get_ptr(i)->translator_cycles > 0) {
			cond_jump_list->get_ptr(i)->translator_cycles -= 1;
		}
	}
}

uint8_t Chip8Engine_JumpHandler::checkConditionalCycle()
{
	for (int32_t i = 0; i < (int32_t)cond_jump_list->size(); i++) {
		uint8_t cycles = cond_jump_list->get_ptr(i)->translator_cycles;
		if (cycles > 0) return cycles;
	}
	return 0;
}

void Chip8Engine_JumpHandler::checkAndFillConditionalJumpsByCycles()
{
	int32_t list_sz = cond_jump_list->size();
	for (int32_t i = 0; i < list_sz; i++) {
		if (cond_jump_list->get_ptr(i)->translator_cycles == 0) {
			int32_t relative = (int32_t)((uint32_t)cache->getEndX86AddressCurrent() - (uint32_t)cond_jump_list->get_ptr(i)->x86_address_jump_value - sizeof(uint32_t)); // 4 is size of uint32_t, as eip is at the end of the jump instruction but we calculate the relative size based on the start address of the relative
			*(cond_jump_list->get_ptr(i)->x86_address_jump_value) = relative;

#ifdef USE_VERBOSE
			char buffer[1000];
			sprintf_s(buffer, 1000, "Conditional Jump[%d] updated! Value %d written to 0x%.8X (in cache[%d]).", i, relative, (uint32_t)cond_jump_list->get_ptr(i)->x86_address_jump_value, cache->findCacheIndexCurrent());
			logMessage(LOGLEVEL::L_INFO, buffer);
#endif

			// remove entry after its been filled
			cond_jump_list->remove(i);
			list_sz = cond_jump_list->size(); // update list size again
			i -= 1; // decrease i by 1 so it rechecks the current i'th value in the list (which would have been i+1 if there was no remove).
		}
	}
}

int32_t Chip8Engine_JumpHandler::findJumpEntry(uint16_t c8_to_)
{
	int32_t index = -1;
	for (int32_t i = 0; i < (int32_t)jump_list->size(); i++) {
		if (c8_to_ == jump_list->get_ptr(i)->c8_address_to) {
			index = i;
			break;
		}
	}
	return index;
}

void Chip8Engine_JumpHandler::checkAndFillJumpsByStartC8PC()
{
	// Function designed to be fast as it will be called many times.
	int32_t list_sz = jump_fill_list->size();
	if (list_sz > 0) {
		CACHE_REGION * region = NULL;
		int32_t jump_list_index;
		int32_t cache_index;

		for (int32_t i = 0; i < list_sz; i++) {
			jump_list_index = jump_fill_list->get(i);
			// Jump cache handling done by CacheHandler, so this function just updates the jump table locations
			cache_index = cache->getCacheWritableByStartC8PC(jump_list->get_ptr(jump_list_index)->c8_address_to);
			region = cache->getCacheInfoByIndex(cache_index);
			jump_list->get_ptr(jump_list_index)->x86_address_to = region->x86_mem_address;

			// remove entry after its been filled
			jump_fill_list->remove(i);
			list_sz = jump_fill_list->size(); // update list size again
			i -= 1; // decrease i by 1 so it rechecks the current i'th value in the list (which would have been i+1 if there was no remove).
		}
	}
}

void Chip8Engine_JumpHandler::clearFilledFlagByC8PC(uint16_t c8_pc)
{
	for (int32_t i = 0; i < (int32_t)jump_list->size(); i++) {
		if (jump_list->get_ptr(i)->c8_address_to == c8_pc) {
			jump_fill_list->push_back(i);
		}
	}
}

int32_t Chip8Engine_JumpHandler::getJumpIndexByC8PC(uint16_t c8_to)
{
	int32_t tblindex = jumptbl->findJumpEntry(c8_to);
	if (tblindex == -1) {
		tblindex = jumptbl->recordJumpEntry(c8_to);
	}
	return tblindex;
}

JUMP_ENTRY * Chip8Engine_JumpHandler::getJumpInfoByIndex(uint32_t index)
{
	return jump_list->get_ptr(index);
}

#ifdef USE_DEBUG_EXTRA
void Chip8Engine_JumpHandler::DEBUG_printJumpList()
{
	for (int32_t i = 0; i < (int32_t)jump_list->size(); i++) {
		char buffer[1000];
		sprintf_s(buffer, 1000, "Jump[%d]: c8_address_to = 0x%.4X, x86_address_to = 0x%.8X.", i, jump_list->get_ptr(i)->c8_address_to, (uint32_t)jump_list->get_ptr(i)->x86_address_to);
		logMessage(LOGLEVEL::L_DEBUG, buffer);
	}
}

void Chip8Engine_JumpHandler::DEBUG_printCondJumpList()
{
	for (int32_t i = 0; i < (int32_t)cond_jump_list->size(); i++) {
		char buffer[1000];
		sprintf_s(buffer, 1000, "CondJump[%d]: c8_address_from = 0x%.4X, c8_address_to = 0x%.4X, x86_address_jump_value = 0x%.8X, translator_cycles = %d.", i, cond_jump_list->get_ptr(i)->c8_address_from, cond_jump_list->get_ptr(i)->c8_address_to, (uint32_t)cond_jump_list->get_ptr(i)->x86_address_jump_value, cond_jump_list->get_ptr(i)->translator_cycles);
		logMessage(LOGLEVEL::L_DEBUG, buffer);
	}
}
#endif