#pragma once

#include "CoreMinimal.h"

#include "FolioCore/FolioHost.h"

class FFolioFileInterface;

/**
 * How FolioCore reaches Unreal for files and logging.
 *
 * RmlUi is a static library, so this module and FolioCore each hold their own copy of its
 * globals. The core therefore cannot call Rml::GetFileInterface() or Rml::Log::Message: it would
 * read a null interface installed by nobody, fail, and say nothing. Routing both through here
 * keeps reads going via the same file interface everything else uses - so mounts resolve and a
 * packaged build works - and puts the core's errors in the log where they can be read.
 */
class FFolioCoreHost final : public Folio::IFolioHost
{
public:
	explicit FFolioCoreHost(FFolioFileInterface& InFiles);

	virtual bool ReadFile(const std::string& Path, std::vector<unsigned char>& OutData) override;
	virtual void LogError(const std::string& Message) override;
	virtual void LogWarning(const std::string& Message) override;

private:
	FFolioFileInterface& Files;
};
