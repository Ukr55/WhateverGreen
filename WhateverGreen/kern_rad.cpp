//
//  kern_rad.cpp
//  WhateverGreen
//
//  Copyright © 2017 vit9696. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Library/LegacyIOService.h>

#include <Availability.h>
#include <IOKit/IOPlatformExpert.h>

#include "kern_rad.hpp"

static const char *pathFramebuffer[]		{ "/System/Library/Extensions/AMDFramebuffer.kext/Contents/MacOS/AMDFramebuffer" };
static const char *pathLegacyFramebuffer[]	{ "/System/Library/Extensions/AMDLegacyFramebuffer.kext/Contents/MacOS/AMDLegacyFramebuffer" };
static const char *pathSupport[]			{ "/System/Library/Extensions/AMDSupport.kext/Contents/MacOS/AMDSupport" };
static const char *pathLegacySupport[]		{ "/System/Library/Extensions/AMDLegacySupport.kext/Contents/MacOS/AMDLegacySupport" };
static const char *pathRadeonX3000[]        { "/System/Library/Extensions/AMDRadeonX3000.kext/Contents/MacOS/AMDRadeonX3000" };
static const char *pathRadeonX4000[]        { "/System/Library/Extensions/AMDRadeonX4000.kext/Contents/MacOS/AMDRadeonX4000" };
static const char *pathRadeonX4100[]        { "/System/Library/Extensions/AMDRadeonX4100.kext/Contents/MacOS/AMDRadeonX4100" };
static const char *pathRadeonX4150[]        { "/System/Library/Extensions/AMDRadeonX4150.kext/Contents/MacOS/AMDRadeonX4150" };
static const char *pathRadeonX4200[]        { "/System/Library/Extensions/AMDRadeonX4200.kext/Contents/MacOS/AMDRadeonX4200" };
static const char *pathRadeonX4250[]        { "/System/Library/Extensions/AMDRadeonX4250.kext/Contents/MacOS/AMDRadeonX4250" };
static const char *kextRadeonX5000[]        { "/System/Library/Extensions/AMDRadeonX5000.kext/Contents/MacOS/AMDRadeonX5000" };

static const char *idRadeonX3000New {"com.apple.kext.AMDRadeonX3000"};
static const char *idRadeonX4000New {"com.apple.kext.AMDRadeonX4000"};
static const char *idRadeonX4100New {"com.apple.kext.AMDRadeonX4100"};
static const char *idRadeonX4150New {"com.apple.kext.AMDRadeonX4150"};
static const char *idRadeonX4200New {"com.apple.kext.AMDRadeonX4200"};
static const char *idRadeonX4250New {"com.apple.kext.AMDRadeonX4250"};
static const char *idRadeonX5000New {"com.apple.kext.AMDRadeonX5000"};
static const char *idRadeonX3000Old {"com.apple.AMDRadeonX3000"};
static const char *idRadeonX4000Old {"com.apple.AMDRadeonX4000"};

static KernelPatcher::KextInfo kextRadeonFramebuffer
{ "com.apple.kext.AMDFramebuffer", pathFramebuffer, arrsize(pathFramebuffer), {}, {}, KernelPatcher::KextInfo::Unloaded };
static KernelPatcher::KextInfo kextRadeonLegacyFramebuffer
{ "com.apple.kext.AMDLegacyFramebuffer", pathLegacyFramebuffer, arrsize(pathLegacyFramebuffer), {}, {}, KernelPatcher::KextInfo::Unloaded };
static KernelPatcher::KextInfo kextRadeonSupport
{ "com.apple.kext.AMDSupport", pathSupport, 1, {}, {}, KernelPatcher::KextInfo::Unloaded };
static KernelPatcher::KextInfo kextRadeonLegacySupport
{ "com.apple.kext.AMDLegacySupport", pathLegacySupport, 1, {}, {}, KernelPatcher::KextInfo::Unloaded };

static KernelPatcher::KextInfo kextRadeonHardware[RAD::MaxRadeonHardware] {
	[RAD::IndexRadeonHardwareX3000] = { idRadeonX3000New, pathRadeonX3000, arrsize(pathRadeonX3000), {}, {}, KernelPatcher::KextInfo::Unloaded },
	[RAD::IndexRadeonHardwareX4100] = { idRadeonX4100New, pathRadeonX4100, arrsize(pathRadeonX4100), {}, {}, KernelPatcher::KextInfo::Unloaded },
	[RAD::IndexRadeonHardwareX4150] = { idRadeonX4150New, pathRadeonX4150, arrsize(pathRadeonX4150), {}, {}, KernelPatcher::KextInfo::Unloaded },
	[RAD::IndexRadeonHardwareX4200] = { idRadeonX4200New, pathRadeonX4200, arrsize(pathRadeonX4200), {}, {}, KernelPatcher::KextInfo::Unloaded },
	[RAD::IndexRadeonHardwareX4250] = { idRadeonX4250New, pathRadeonX4250, arrsize(pathRadeonX4250), {}, {}, KernelPatcher::KextInfo::Unloaded },
	[RAD::IndexRadeonHardwareX4000] = { idRadeonX4000New, pathRadeonX4000, arrsize(pathRadeonX4000), {}, {}, KernelPatcher::KextInfo::Unloaded },
	[RAD::IndexRadeonHardwareX5000] = { idRadeonX5000New, kextRadeonX5000, arrsize(kextRadeonX5000), {}, {}, KernelPatcher::KextInfo::Unloaded }
};

/**
 *  Power-gating flags
 *  Each symbol corresponds to a bit provided in a radpg argument mask
 */
static const char *powerGatingFlags[] {
	"CAIL_DisableDrmdmaPowerGating",
	"CAIL_DisableGfxCGPowerGating",
	"CAIL_DisableUVDPowerGating",
	"CAIL_DisableVCEPowerGating",
	"CAIL_DisableDynamicGfxMGPowerGating",
	"CAIL_DisableGmcPowerGating",
	"CAIL_DisableAcpPowerGating",
	"CAIL_DisableSAMUPowerGating"
};

RAD *RAD::callbackRAD;

void RAD::init() {
	callbackRAD = this;

	// Certain displays do not support 32-bit colour output, so we have to force 24-bit.
	if (getKernelVersion() >= KernelVersion::Sierra && checkKernelArgument("-rad24")) {
		lilu.onKextLoadForce(&kextRadeonFramebuffer);
		// Mojave dropped legacy GPU support (5xxx and 6xxx).
		if (getKernelVersion() < KernelVersion::Mojave)
			lilu.onKextLoadForce(&kextRadeonLegacyFramebuffer);
	}

	// Certain GPUs cannot output to DVI at full resolution.
	dviSingleLink = checkKernelArgument("-raddvi");

	// Disabling Metal may be useful for testing
	forceOpenGL = checkKernelArgument("-radgl");

	// Fix accelerator name if requested
	fixConfigName = checkKernelArgument("-radcfg");

	// Broken drivers can still let us boot in vesa mode
	forceVesaMode = checkKernelArgument("-radvesa");

	// To support overriding connectors and -radvesa mode we need to patch AMDSupport.
	lilu.onKextLoadForce(&kextRadeonSupport);
	// Mojave dropped legacy GPU support (5xxx and 6xxx).
	if (getKernelVersion() < KernelVersion::Mojave)
		lilu.onKextLoadForce(&kextRadeonLegacySupport);

	initHardwareKextMods();

	//FIXME: autodetect?
	uint32_t powerGatingMask = 0;
	PE_parse_boot_argn("radpg", &powerGatingMask, sizeof(powerGatingMask));
	for (size_t i = 0; i < arrsize(powerGatingFlags); i++) {
		if (!(powerGatingMask & (1 << i))) {
			DBGLOG("rad", "not enabling %s", powerGatingFlags[i]);
			powerGatingFlags[i] = nullptr;
		} else {
			DBGLOG("rad", "enabling %s", powerGatingFlags[i]);
		}
	}
}

void RAD::deinit() {

}

void RAD::processKernel(KernelPatcher &patcher, DeviceInfo *info) {
	bool hasAMD = false;
	for (size_t i = 0; i < info->videoExternal.size(); i++) {
		if (info->videoExternal[i].vendor == WIOKit::VendorID::ATIAMD) {
			hasAMD = true;
			break;
		}
	}

	if (hasAMD) {
		KernelPatcher::RouteRequest requests[] {
			KernelPatcher::RouteRequest("__ZN15IORegistryEntry11setPropertyEPKcPvj", wrapSetProperty, orgSetProperty),
			KernelPatcher::RouteRequest("__ZNK15IORegistryEntry11getPropertyEPKc", wrapGetProperty, orgGetProperty),
		};
		patcher.routeMultiple(KernelPatcher::KernelID, requests);
	} else {
		kextRadeonFramebuffer.switchOff();
		kextRadeonLegacyFramebuffer.switchOff();
		kextRadeonSupport.switchOff();
		kextRadeonLegacySupport.switchOff();

		for (size_t i = 0; i < maxHardwareKexts; i++)
			kextRadeonHardware[i].switchOff();
	}
}

bool RAD::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (kextRadeonFramebuffer.loadIndex == index) {
		process24BitOutput(patcher, kextRadeonFramebuffer, address, size);
		return true;
	}

	if (kextRadeonLegacyFramebuffer.loadIndex == index) {
		process24BitOutput(patcher, kextRadeonLegacyFramebuffer, address, size);
		return true;
	}

	if (kextRadeonSupport.loadIndex == index) {
		processConnectorOverrides(patcher, address, size, true);
		return true;
	}

	if (kextRadeonLegacySupport.loadIndex == index) {
		processConnectorOverrides(patcher, address, size, false);
		return true;
	}

	for (size_t i = 0; i < maxHardwareKexts; i++) {
		if (kextRadeonHardware[i].loadIndex == index) {
			processHardwareKext(patcher, i, address, size);
			return true;
		}
	}

	return false;
}

void RAD::initHardwareKextMods() {
	// Decide on kext amount present for optimal performance.
	// 10.14+   has X4000 and X5000
	// 10.13.4+ has X3000, X4000, and X5000
	if (getKernelVersion() >= KernelVersion::Mojave)
		maxHardwareKexts = MaxRadeonHardwareMojave;
	else if (getKernelVersion() == KernelVersion::HighSierra && getKernelMinorVersion() >= 5)
		maxHardwareKexts = MaxRadeonHardwareModernHighSierra;

	// 10.13.4 fixed black screen issues
	if (maxHardwareKexts != MaxRadeonHardware) {
		for (size_t i = 0; i < MaxGetFrameBufferProcs; i++)
			getFrameBufferProcNames[IndexRadeonHardwareX4000][i] = nullptr;

		// We have nothing to do for these kexts on recent systems
		if (!fixConfigName && !forceOpenGL) {
			kextRadeonHardware[IndexRadeonHardwareX4000].switchOff();
			kextRadeonHardware[IndexRadeonHardwareX5000].switchOff();
		}
	}

	if (getKernelVersion() < KernelVersion::HighSierra) {
		// Versions before 10.13 do not support X4250 and X5000
		kextRadeonHardware[IndexRadeonHardwareX4250].switchOff();
		kextRadeonHardware[IndexRadeonHardwareX5000].switchOff();

		// Versions before 10.13 have legacy X3000 and X4000 IDs
		kextRadeonHardware[IndexRadeonHardwareX3000].id = idRadeonX3000Old;
		kextRadeonHardware[IndexRadeonHardwareX4000].id = idRadeonX4000Old;

		bool preSierra = getKernelVersion() < KernelVersion::Sierra;

		if (preSierra) {
			// Versions before 10.12 do not support X4100
			kextRadeonHardware[IndexRadeonHardwareX4100].switchOff();
		}

		if (preSierra || (getKernelVersion() == KernelVersion::Sierra && getKernelMinorVersion() < 7)) {
			// Versions before 10.12.6 do not support X4150, X4200
			kextRadeonHardware[IndexRadeonHardwareX4150].switchOff();
			kextRadeonHardware[IndexRadeonHardwareX4200].switchOff();
		}
	}

	lilu.onKextLoadForce(kextRadeonHardware, maxHardwareKexts);
}

void RAD::process24BitOutput(KernelPatcher &patcher, KernelPatcher::KextInfo &info, mach_vm_address_t address, size_t size) {
	auto bitsPerComponent = patcher.solveSymbol<int *>(info.loadIndex, "__ZL18BITS_PER_COMPONENT", address, size);
	if (bitsPerComponent) {
		while (bitsPerComponent && *bitsPerComponent) {
			if (*bitsPerComponent == 10) {
				auto ret = MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock);
				if (ret == KERN_SUCCESS) {
					DBGLOG("rad", "fixing BITS_PER_COMPONENT");
					*bitsPerComponent = 8;
					MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
				} else {
					SYSLOG("rad", "failed to disable write protection for BITS_PER_COMPONENT");
				}
			}
			bitsPerComponent++;
		}
	} else {
		SYSLOG("rad", "failed to find BITS_PER_COMPONENT");
	}

	DBGLOG("rad", "fixing pixel types");

	KernelPatcher::LookupPatch pixelPatch {
		&info,
		reinterpret_cast<const uint8_t *>("--RRRRRRRRRRGGGGGGGGGGBBBBBBBBBB"),
		reinterpret_cast<const uint8_t *>("--------RRRRRRRRGGGGGGGGBBBBBBBB"),
		32, 2
	};

	patcher.applyLookupPatch(&pixelPatch);
	if (patcher.getError() != KernelPatcher::Error::NoError) {
		SYSLOG("rad", "failed to patch RGB mask for 24-bit output");
		patcher.clearError();
	}
}

void RAD::processConnectorOverrides(KernelPatcher &patcher, mach_vm_address_t address, size_t size, bool modern) {
	if (modern) {
		if (getKernelVersion() >= KernelVersion::HighSierra) {
			KernelPatcher::RouteRequest requests[] {
				KernelPatcher::RouteRequest("__ZN14AtiBiosParser116getConnectorInfoEP13ConnectorInfoRh", wrapGetConnectorsInfoV1, orgGetConnectorsInfoV1),
				KernelPatcher::RouteRequest("__ZN14AtiBiosParser216getConnectorInfoEP13ConnectorInfoRh", wrapGetConnectorsInfoV2, orgGetConnectorsInfoV2),
				KernelPatcher::RouteRequest("__ZN14AtiBiosParser126translateAtomConnectorInfoERN30AtiObjectInfoTableInterface_V117AtomConnectorInfoER13ConnectorInfo",
											wrapTranslateAtomConnectorInfoV1, orgTranslateAtomConnectorInfoV1),
				KernelPatcher::RouteRequest("__ZN14AtiBiosParser226translateAtomConnectorInfoERN30AtiObjectInfoTableInterface_V217AtomConnectorInfoER13ConnectorInfo",
											wrapTranslateAtomConnectorInfoV2, orgTranslateAtomConnectorInfoV2),
				KernelPatcher::RouteRequest("__ZN13ATIController5startEP9IOService", wrapATIControllerStart, orgATIControllerStart),
			};
			patcher.routeMultiple(kextRadeonSupport.loadIndex, requests, address, size);
		} else {
			KernelPatcher::RouteRequest requests[] {
				KernelPatcher::RouteRequest("__ZN23AtiAtomBiosDceInterface17getConnectorsInfoEP13ConnectorInfoRh", wrapGetConnectorsInfoV1, orgGetConnectorsInfoV1),
				KernelPatcher::RouteRequest("__ZN13ATIController5startEP9IOService", wrapATIControllerStart, orgATIControllerStart),
			};
			patcher.routeMultiple(kextRadeonSupport.loadIndex, requests, address, size);

			orgGetAtomObjectTableForType = reinterpret_cast<t_getAtomObjectTableForType>(patcher.solveSymbol(kextRadeonSupport.loadIndex,
																											 "__ZN20AtiAtomBiosUtilities25getAtomObjectTableForTypeEhRh", address, size));
			if (!orgGetAtomObjectTableForType) {
				SYSLOG("rad", "failed to find AtiAtomBiosUtilities::getAtomObjectTableForType");
				patcher.clearError();
			}
		}
	} else {
		KernelPatcher::RouteRequest requests[] {
			KernelPatcher::RouteRequest("__ZN23AtiAtomBiosDceInterface17getConnectorsInfoEP13ConnectorInfoRh", wrapLegacyGetConnectorsInfo, orgLegacyGetConnectorsInfo),
			KernelPatcher::RouteRequest("__ZN19AMDLegacyController5startEP9IOService", wrapLegacyATIControllerStart, orgLegacyATIControllerStart),
		};
		patcher.routeMultiple(kextRadeonLegacySupport.loadIndex, requests, address, size);

		orgLegacyGetAtomObjectTableForType = patcher.solveSymbol<t_getAtomObjectTableForType>(kextRadeonLegacySupport.loadIndex,
																							  "__ZN20AtiAtomBiosUtilities25getAtomObjectTableForTypeEhRh", address, size);
		if (!orgLegacyGetAtomObjectTableForType) {
			SYSLOG("rad", "failed to find AtiAtomBiosUtilities::getAtomObjectTableForType");
			patcher.clearError();
		}
	}
}

void RAD::processHardwareKext(KernelPatcher &patcher, size_t hwIndex, mach_vm_address_t address, size_t size) {
	auto getFrame = getFrameBufferProcNames[hwIndex];
	auto &hardware = kextRadeonHardware[hwIndex];

	// Fix boot and wake to black screen
	for (size_t j = 0; j < MaxGetFrameBufferProcs && getFrame[j] != nullptr; j++) {
		auto getFB = patcher.solveSymbol(hardware.loadIndex, getFrame[j], address, size);
		if (getFB) {
			// Initially it was discovered that the only problematic register is PRIMARY_SURFACE_ADDRESS_HIGH (0x1A07).
			// This register must be nulled to solve most of the issues.
			// Depending on the amount of connected screens PRIMARY_SURFACE_ADDRESS (0x1A04) may not be null.
			// However, as of AMD Vega drivers in 10.13 DP1 both of these registers are now ignored.
			// Furthermore, there are no (extra) issues from just returning 0 in framebuffer base address.

			// xor rax, rax
			// ret
			uint8_t ret[] {0x48, 0x31, 0xC0, 0xC3};
			patcher.routeBlock(getFB, ret, sizeof(ret));
			if (patcher.getError() == KernelPatcher::Error::NoError) {
				DBGLOG("rad", "patched %s", getFrame[j]);
			} else {
				SYSLOG("rad", "failed to patch %s code %d", getFrame[j], patcher.getError());
				patcher.clearError();
			}
		} else {
			SYSLOG("rad", "failed to find %s code %d", getFrame[j], patcher.getError());
			patcher.clearError();
		}
	}

	// Fix reported Accelerator name to support WhateverName.app
	if (fixConfigName) {
		KernelPatcher::RouteRequest request(populateAccelConfigProcNames[hwIndex], wrapPopulateAccelConfig[hwIndex], orgPopulateAccelConfig[hwIndex]);
		patcher.routeMultiple(hardware.loadIndex, &request, 1, address, size);
	}

	// Enforce OpenGL support if requested
	if (forceOpenGL) {
		DBGLOG("rad", "disabling Metal support");
		uint8_t find1[] {0x4D, 0x65, 0x74, 0x61, 0x6C, 0x53, 0x74, 0x61};
		uint8_t find2[] {0x4D, 0x65, 0x74, 0x61, 0x6C, 0x50, 0x6C, 0x75};
		uint8_t repl1[] {0x50, 0x65, 0x74, 0x61, 0x6C, 0x53, 0x74, 0x61};
		uint8_t repl2[] {0x50, 0x65, 0x74, 0x61, 0x6C, 0x50, 0x6C, 0x75};

		KernelPatcher::LookupPatch antimetal[] {
			{&hardware, find1, repl1, sizeof(find1), 2},
			{&hardware, find2, repl2, sizeof(find1), 2}
		};

		for (auto &p : antimetal) {
			patcher.applyLookupPatch(&p);
			patcher.clearError();
		}
	}
}

void RAD::mergeProperties(OSDictionary *props, const char *prefix, IOService *provider) {
	// Should be ok, but in case there are issues switch to dictionaryWithProperties();
	auto dict = provider->getPropertyTable();
	if (dict) {
		auto iterator = OSCollectionIterator::withCollection(dict);
		if (iterator) {
			OSSymbol *propname;
			size_t prefixlen = strlen(prefix);
			while ((propname = OSDynamicCast(OSSymbol, iterator->getNextObject())) != nullptr) {
				auto name = propname->getCStringNoCopy();
				if (name && propname->getLength() > prefixlen && !strncmp(name, prefix, prefixlen)) {
					auto prop = dict->getObject(propname);
					if (prop) {
						// It is hard to make a boolean from ACPI, so we make a hack here:
						// 1-byte OSData with 0x01 / 0x00 values becomes boolean.
						auto data = OSDynamicCast(OSData, prop);
						if (data && data->getLength() == 1) {
							auto val = static_cast<const uint8_t *>(data->getBytesNoCopy());
							if (val && val[0] == 1) {
								props->setObject(name+prefixlen, kOSBooleanTrue);
								DBGLOG("rad", "prop %s was merged as kOSBooleanTrue", name);
								continue;
							} else if (val && val[0] == 0) {
								props->setObject(name+prefixlen, kOSBooleanFalse);
								DBGLOG("rad", "prop %s was merged as kOSBooleanFalse", name);
								continue;
							}
						}
						
						props->setObject(name+prefixlen, prop);
						DBGLOG("rad", "prop %s was merged", name);
					} else {
						DBGLOG("rad", "prop %s was not merged due to no value", name);
					}
				} else {
					//DBGLOG("rad", "prop %s does not match %s prefix", safeString(name), prefix);
				}
			}

			iterator->release();
		} else {
			SYSLOG("rad", "prop merge failed to iterate over properties");
		}
	} else {
		SYSLOG("rad", "prop merge failed to get properties");
	}
	
	if (!strcmp(prefix, "CAIL,")) {
		for (size_t i = 0; i < arrsize(powerGatingFlags); i++) {
			if (powerGatingFlags[i] && props->getObject(powerGatingFlags[i])) {
				DBGLOG("rad", "cail prop merge found %s, replacing", powerGatingFlags[i]);
				props->setObject(powerGatingFlags[i], OSNumber::withNumber(1, 32));
			}
		}
	}
}

void RAD::applyPropertyFixes(IOService *service, uint32_t connectorNum) {
	if (service && getKernelVersion() >= KernelVersion::HighSierra) {
		// Starting with 10.13.2 this is important to fix sleep issues due to enforced 6 screens
		if (!service->getProperty("CFG,CFG_FB_LIMIT")) {
			DBGLOG("rad", "setting fb limit to %d", connectorNum);
			service->setProperty("CFG_FB_LIMIT", OSNumber::withNumber(connectorNum, 32));
		}

		// In the past we set CFG_USE_AGDC to false, which caused visual glitches and broken multimonitor support.
		// A better workaround is to disable AGDP just like we do globally.
	}
}

void RAD::updateConnectorsInfo(void *atomutils, t_getAtomObjectTableForType gettable, IOService *ctrl, RADConnectors::Connector *connectors, uint8_t *sz) {
	if (atomutils) {
		DBGLOG("rad", "getConnectorsInfo found %d connectors", *sz);
		RADConnectors::print(connectors, *sz);
	}

	// Check if the user wants to override automatically detected connectors
	auto cons = ctrl->getProperty("connectors");
	if (cons) {
		auto consData = OSDynamicCast(OSData, cons);
		if (consData) {
			auto consPtr = consData->getBytesNoCopy();
			auto consSize = consData->getLength();

			uint32_t consCount;
			if (WIOKit::getOSDataValue(ctrl, "connector-count", consCount)) {
				*sz = consCount;
				DBGLOG("rad", "getConnectorsInfo got size override to %d", *sz);
			}

			if (consPtr && consSize > 0 && *sz > 0 && RADConnectors::valid(consSize, *sz)) {
				RADConnectors::copy(connectors, *sz, static_cast<const RADConnectors::Connector *>(consPtr), consSize);
				DBGLOG("rad", "getConnectorsInfo installed %d connectors", *sz);
				applyPropertyFixes(ctrl, consSize);
			} else {
				DBGLOG("rad", "getConnectorsInfo conoverrides have invalid size %d for %d num", consSize, *sz);
			}
		} else {
			DBGLOG("rad", "getConnectorsInfo conoverrides have invalid type");
		}
	} else {
		if (atomutils) {
			DBGLOG("rad", "getConnectorsInfo attempting to autofix connectors");
			uint8_t sHeader = 0, displayPathNum = 0, connectorObjectNum = 0;
			auto baseAddr = static_cast<uint8_t *>(gettable(atomutils, AtomObjectTableType::Common, &sHeader)) - sizeof(uint32_t);
			auto displayPaths = static_cast<AtomDisplayObjectPath *>(gettable(atomutils, AtomObjectTableType::DisplayPath, &displayPathNum));
			auto connectorObjects = static_cast<AtomConnectorObject *>(gettable(atomutils, AtomObjectTableType::ConnectorObject, &connectorObjectNum));
			if (displayPathNum == connectorObjectNum)
				autocorrectConnectors(baseAddr, displayPaths, displayPathNum, connectorObjects, connectorObjectNum, connectors, *sz);
			else
				DBGLOG("rad", "getConnectorsInfo found different displaypaths %d and connectors %d", displayPathNum, connectorObjectNum);
		}

		applyPropertyFixes(ctrl, *sz);

		// Prioritise connectors, since it may cause black screen on e.g. R9 370
		const uint8_t *senseList = nullptr;
		uint8_t senseNum = 0;
		auto priData = OSDynamicCast(OSData, ctrl->getProperty("connector-priority"));
		if (priData) {
			senseList = static_cast<const uint8_t *>(priData->getBytesNoCopy());
			senseNum = static_cast<uint8_t>(priData->getLength());
			DBGLOG("rad", "getConnectorInfo found %d senses in connector-priority", senseNum);
			reprioritiseConnectors(senseList, senseNum, connectors, *sz);
		} else {
			DBGLOG("rad", "getConnectorInfo leaving unchaged priority");
		}
	}

	DBGLOG("rad", "getConnectorsInfo resulting %d connectors follow", *sz);
	RADConnectors::print(connectors, *sz);
}

void RAD::autocorrectConnectors(uint8_t *baseAddr, AtomDisplayObjectPath *displayPaths, uint8_t displayPathNum, AtomConnectorObject *connectorObjects,
								uint8_t connectorObjectNum, RADConnectors::Connector *connectors, uint8_t sz) {
	for (uint8_t i = 0; i < displayPathNum; i++) {
		if (!isEncoder(displayPaths[i].usGraphicObjIds)) {
			DBGLOG("rad", "autocorrectConnectors not encoder %X at %d", displayPaths[i].usGraphicObjIds, i);
			continue;
		}
		
		uint8_t txmit = 0, enc = 0;
		if (!getTxEnc(displayPaths[i].usGraphicObjIds, txmit, enc))
			continue;
		
		uint8_t sense = getSenseID(baseAddr + connectorObjects[i].usRecordOffset);
		if (!sense) {
			DBGLOG("rad", "autocorrectConnectors failed to detect sense for %d connector", i);
			continue;
		}
		
		DBGLOG("rad", "autocorrectConnectors found txmit %02X enc %02X sense %02X for %d connector", txmit, enc, sense, i);

		autocorrectConnector(getConnectorID(displayPaths[i].usConnObjectId), sense, txmit, enc, connectors, sz);
	}
}

void RAD::autocorrectConnector(uint8_t connector, uint8_t sense, uint8_t txmit, uint8_t enc, RADConnectors::Connector *connectors, uint8_t sz) {
	// This function attempts to fix the following issues:
	//
	// 1. Incompatible DVI transmitter on 290X, 370 and probably some other models
	// In this case a correct transmitter is detected by AtiAtomBiosDce60::getPropertiesForEncoderObject, however, later
	// in AtiAtomBiosDce60::getPropertiesForConnectorObject for DVI DL and TITFP513 this value is conjuncted with 0xCF,
	// which makes it wrong: 0x10 -> 0, 0x11 -> 1. As a result one gets black screen when connecting multiple displays.
	// getPropertiesForEncoderObject takes usGraphicObjIds and getPropertiesForConnectorObject takes usConnObjectId
	
	if (callbackRAD->dviSingleLink) {
		if (connector != CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I &&
			connector != CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D &&
			connector != CONNECTOR_OBJECT_ID_LVDS) {
			DBGLOG("rad", "autocorrectConnector found unsupported connector type %02X", connector);
			return;
		}
		
		auto fixTransmit = [](auto &con, uint8_t idx, uint8_t sense, uint8_t txmit) {
			if (con.sense == sense) {
				if (con.transmitter != txmit && (con.transmitter & 0xCF) == con.transmitter) {
					DBGLOG("rad", "autocorrectConnector replacing txmit %02X with %02X for %d connector sense %02X",
						   con.transmitter, txmit, idx, sense);
					con.transmitter = txmit;
				}
				return true;
			}
			return false;
		};
		
		bool isModern = RADConnectors::modern();
		for (uint8_t j = 0; j < sz; j++) {
			if (isModern) {
				auto &con = (&connectors->modern)[j];
				if (fixTransmit(con, j, sense, txmit))
					break;
			} else {
				auto &con = (&connectors->legacy)[j];
				if (fixTransmit(con, j, sense, txmit))
					break;
			}
		}
	} else {
		DBGLOG("rad", "autocorrectConnector use -raddvi to enable dvi autocorrection");
	}
}

void RAD::reprioritiseConnectors(const uint8_t *senseList, uint8_t senseNum, RADConnectors::Connector *connectors, uint8_t sz) {
	static constexpr uint32_t typeList[] {
		RADConnectors::ConnectorLVDS,
		RADConnectors::ConnectorDigitalDVI,
		RADConnectors::ConnectorHDMI,
		RADConnectors::ConnectorDP,
		RADConnectors::ConnectorVGA
	};
	static constexpr uint8_t typeNum {static_cast<uint8_t>(arrsize(typeList))};
	
	bool isModern = RADConnectors::modern();
	uint16_t priCount = 1;
	// Automatically detected connectors have equal priority (0), which often results in black screen
	// This allows to change this firstly by user-defined list, then by type list.
	//TODO: priority is ignored for 5xxx and 6xxx GPUs, should we manually reorder items?
	for (uint8_t i = 0; i < senseNum + typeNum + 1; i++) {
		for (uint8_t j = 0; j < sz; j++) {
			auto reorder = [&](auto &con) {
				if (i == senseNum + typeNum) {
					if (con.priority == 0)
						con.priority = priCount++;
				} else if (i < senseNum) {
					if (con.sense == senseList[i]) {
						DBGLOG("rad", "reprioritiseConnectors setting priority of sense %02X to %d by sense", con.sense, priCount);
						con.priority = priCount++;
						return true;
					}
				} else {
					if (con.priority == 0 && con.type == typeList[i-senseNum]) {
						DBGLOG("rad", "reprioritiseConnectors setting priority of sense %02X to %d by type", con.sense, priCount);
						con.priority = priCount++;
					}
				}
				return false;
			};
			
			if ((isModern && reorder((&connectors->modern)[j])) ||
				(!isModern && reorder((&connectors->legacy)[j])))
				break;
		}
	}
}

void RAD::updateAccelConfig(IOService *accelService, const char **accelConfig) {
	if (accelService && accelConfig) {
		auto gpuService = accelService->getParentEntry(gIOServicePlane);

		if (gpuService) {
			auto model = OSDynamicCast(OSData, gpuService->getProperty("model"));
			if (model) {
				auto modelStr = static_cast<const char *>(model->getBytesNoCopy());
				if (modelStr) {
					if (modelStr[0] == 'A' && ((modelStr[1] == 'M' && modelStr[2] == 'D') ||
											   (modelStr[1] == 'T' && modelStr[2] == 'I')) && modelStr[3] == ' ') {
						modelStr += 4;
					}

					DBGLOG("rad", "updateAccelConfig found gpu model %s", modelStr);
					*accelConfig = modelStr;
				} else {
					DBGLOG("rad", "updateAccelConfig found null gpu model");
				}
			} else {
				DBGLOG("rad", "updateAccelConfig failed to find gpu model");
			}

		} else {
			DBGLOG("rad", "updateAccelConfig failed to find accelerator parent");
		}
	}
}


bool RAD::wrapSetProperty(IORegistryEntry *that, const char *aKey, void *bytes, unsigned length) {
	if (length > 10 && aKey && reinterpret_cast<const uint32_t *>(aKey)[0] == 'edom' && reinterpret_cast<const uint16_t *>(aKey)[2] == 'l') {
		DBGLOG("rad", "SetProperty caught model %d (%.*s)", length, length, static_cast<char *>(bytes));
		if (*static_cast<uint32_t *>(bytes) == ' DMA' || *static_cast<uint32_t *>(bytes) == ' ITA') {
			if (FunctionCast(wrapGetProperty, callbackRAD->orgGetProperty)(that, aKey)) {
				DBGLOG("rad", "SetProperty ignored setting %s to %s", aKey, static_cast<char *>(bytes));
				return true;
			}
			DBGLOG("rad", "SetProperty missing %s, fallback to %s", aKey, static_cast<char *>(bytes));
		}
	}

	return FunctionCast(wrapSetProperty, callbackRAD->orgSetProperty)(that, aKey, bytes, length);
}

OSObject *RAD::wrapGetProperty(IORegistryEntry *that, const char *aKey) {
	auto obj = FunctionCast(wrapGetProperty, callbackRAD->orgGetProperty)(that, aKey);
	auto props = OSDynamicCast(OSDictionary, obj);

	if (props && aKey) {
		const char *prefix {nullptr};
		auto provider = callbackRAD->currentLegacyPropProvider;
		if (!provider)
			provider = callbackRAD->currentPropProvider;
		if (provider) {
			if (aKey[0] == 'a') {
				if (!strcmp(aKey, "aty_config"))
					prefix = "CFG,";
				else if (!strcmp(aKey, "aty_properties"))
					prefix = "PP,";
			}
		} else if (aKey[0] == 'c' && !strcmp(aKey, "cail_properties")) {
			provider = OSDynamicCast(IOService, that->getParentEntry(gIOServicePlane));
			DBGLOG("rad", "GetProperty got cail_properties %d, merging from %s", provider != nullptr,
				   provider ? safeString(provider->getName()) : "(null provider)");
			if (provider) prefix = "CAIL,";
		}

		if (prefix) {
			DBGLOG("rad", "GetProperty discovered property merge request for %s", aKey);
			auto newProps = OSDynamicCast(OSDictionary, props->copyCollection());
			callbackRAD->mergeProperties(newProps, prefix, provider);
			that->setProperty(aKey, newProps);
			obj = newProps;
		}
	}

	return obj;
}

uint32_t RAD::wrapGetConnectorsInfoV1(void *that, RADConnectors::Connector *connectors, uint8_t *sz) {
	uint32_t code = FunctionCast(wrapGetConnectorsInfoV1, callbackRAD->orgGetConnectorsInfoV1)(that, connectors, sz);
	auto props = callbackRAD->currentPropProvider;
	if (code == 0 && sz && props) {
		if (getKernelVersion() >= KernelVersion::HighSierra)
			callbackRAD->updateConnectorsInfo(nullptr, nullptr, props, connectors, sz);
		else
			callbackRAD->updateConnectorsInfo(static_cast<void **>(that)[1], callbackRAD->orgGetAtomObjectTableForType, props, connectors, sz);
	} else {
		DBGLOG("rad", "getConnectorsInfoV1 failed %X or undefined %d", code, props == nullptr);
	}
	
	return code;
}

uint32_t RAD::wrapGetConnectorsInfoV2(void *that, RADConnectors::Connector *connectors, uint8_t *sz) {
	uint32_t code = FunctionCast(wrapGetConnectorsInfoV2, callbackRAD->orgGetConnectorsInfoV2)(that, connectors, sz);
	auto props = callbackRAD->currentPropProvider;
	if (code == 0 && sz && props)
		callbackRAD->updateConnectorsInfo(nullptr, nullptr, props, connectors, sz);
	else
		DBGLOG("rad", "getConnectorsInfoV2 failed %X or undefined %d", code, props == nullptr);
	
	return code;
}

uint32_t RAD::wrapLegacyGetConnectorsInfo(void *that, RADConnectors::Connector *connectors, uint8_t *sz) {
	uint32_t code = FunctionCast(wrapLegacyGetConnectorsInfo, callbackRAD->orgLegacyGetConnectorsInfo)(that, connectors, sz);
	auto props = callbackRAD->currentLegacyPropProvider;
	if (code == 0 && sz && props)
		callbackRAD->updateConnectorsInfo(static_cast<void **>(that)[1], callbackRAD->orgLegacyGetAtomObjectTableForType, props, connectors, sz);
	else
		DBGLOG("rad", "legacy getConnectorsInfo failed %X or undefined %d", code, props == nullptr);
	
	return code;
}

uint32_t RAD::wrapTranslateAtomConnectorInfoV1(void *that, RADConnectors::AtomConnectorInfo *info, RADConnectors::Connector *connector) {
	uint32_t code = FunctionCast(wrapTranslateAtomConnectorInfoV1, callbackRAD->orgTranslateAtomConnectorInfoV1)(that, info, connector);
	
	if (code == 0 && info && connector) {
		RADConnectors::print(connector, 1);
		
		uint8_t sense = getSenseID(info->i2cRecord);
		if (sense) {
			DBGLOG("rad", "translateAtomConnectorInfoV1 got sense id %02X", sense);
			
			// We need to extract usGraphicObjIds from info->hpdRecord, which is of type ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT:
			// struct ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT {
			//   uint8_t ucNumberOfSrc;
			//   uint16_t usSrcObjectID[ucNumberOfSrc];
			//   uint8_t ucNumberOfDst;
			//   uint16_t usDstObjectID[ucNumberOfDst];
			// };
			// The value we need is in usSrcObjectID. The structure is byte-packed.
			
			uint8_t ucNumberOfSrc = info->hpdRecord[0];
			for (uint8_t i = 0; i < ucNumberOfSrc; i++) {
				auto usSrcObjectID = *reinterpret_cast<uint16_t *>(info->hpdRecord + sizeof(uint8_t) + i * sizeof(uint16_t));
				DBGLOG("rad", "translateAtomConnectorInfoV1 checking %04X object id", usSrcObjectID);
				if (((usSrcObjectID & OBJECT_TYPE_MASK) >> OBJECT_TYPE_SHIFT) == GRAPH_OBJECT_TYPE_ENCODER) {
					uint8_t txmit = 0, enc = 0;
					if (getTxEnc(usSrcObjectID, txmit, enc))
						callbackRAD->autocorrectConnector(getConnectorID(info->usConnObjectId), getSenseID(info->i2cRecord), txmit, enc, connector, 1);
					break;
				}
			}
			
			
		} else {
			DBGLOG("rad", "translateAtomConnectorInfoV1 failed to detect sense for translated connector");
		}
	}
	
	return code;
}

uint32_t RAD::wrapTranslateAtomConnectorInfoV2(void *that, RADConnectors::AtomConnectorInfo *info, RADConnectors::Connector *connector) {
	uint32_t code = FunctionCast(wrapTranslateAtomConnectorInfoV2, callbackRAD->orgTranslateAtomConnectorInfoV2)(that, info, connector);
	
	if (code == 0 && info && connector) {
		RADConnectors::print(connector, 1);
		
		uint8_t sense = getSenseID(info->i2cRecord);
		if (sense) {
			DBGLOG("rad", "translateAtomConnectorInfoV2 got sense id %02X", sense);
			uint8_t txmit = 0, enc = 0;
			if (getTxEnc(info->usGraphicObjIds, txmit, enc))
				callbackRAD->autocorrectConnector(getConnectorID(info->usConnObjectId), getSenseID(info->i2cRecord), txmit, enc, connector, 1);
		} else {
			DBGLOG("rad", "translateAtomConnectorInfoV2 failed to detect sense for translated connector");
		}
	}
	
	return code;
}

bool RAD::wrapATIControllerStart(IOService *ctrl, IOService *provider) {
	DBGLOG("rad", "starting controller");
	if (callbackRAD->forceVesaMode) {
		DBGLOG("rad", "disabling video acceleration on request");
		return false;
	}
	
	callbackRAD->currentPropProvider = provider;
	bool r = FunctionCast(wrapATIControllerStart, callbackRAD->orgATIControllerStart)(ctrl, provider);
	callbackRAD->currentPropProvider = nullptr;
	DBGLOG("rad", "starting controller done %d", r);
	return r;
}

bool RAD::wrapLegacyATIControllerStart(IOService *ctrl, IOService *provider) {
	DBGLOG("rad", "starting legacy controller");
	if (callbackRAD->forceVesaMode) {
		DBGLOG("rad", "disabling legacy video acceleration on request");
		return false;
	}
	
	callbackRAD->currentLegacyPropProvider = provider;
	bool r = FunctionCast(wrapLegacyATIControllerStart, callbackRAD->orgLegacyATIControllerStart)(ctrl, provider);
	callbackRAD->currentLegacyPropProvider = nullptr;
	DBGLOG("rad", "starting legacy controller done %d", r);
	return r;
}
