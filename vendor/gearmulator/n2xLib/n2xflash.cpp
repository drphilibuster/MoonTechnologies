#include "n2xflash.h"

#include "n2xhardware.h"
#include "n2xtypes.h"

#include "baseLib/filesystem.h"

#include "synthLib/os.h"

namespace n2x
{
	Flash::Flash(Hardware& _hardware) : m_hardware(_hardware)
	{
		// MoonTechnologies: no search of the disk for "any 64 KB file" in a plugin: the flash
		// starts erased and the module loads its contents with setData() from the patch.
	}
}
