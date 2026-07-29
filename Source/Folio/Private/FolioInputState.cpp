#include "FolioInputState.h"

#include "FolioModule.h"
#include "FolioNavigation.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

FFolioInputModel::~FFolioInputModel()
{
	Release();
}

void FFolioInputModel::Create(Rml::Context* Context)
{
	if (!Context || Handle)
	{
		return;
	}

	Rml::DataModelConstructor Constructor = Context->CreateDataModel(Folio::InputModel::Name());
	if (!Constructor)
	{
		UE_LOG(LogFolio, Warning, TEXT("could not create the reserved '%hs' data model"),
			Folio::InputModel::Name());
		return;
	}

	Constructor.Bind(Folio::InputModel::Device(), &DeviceValue);
	Constructor.Bind(Folio::InputModel::Gamepad(), &bGamepad);
	Constructor.Bind(Folio::InputModel::Confirm(), &ConfirmValue);
	Constructor.Bind(Folio::InputModel::Back(), &BackValue);
	Handle = Constructor.GetModelHandle();

	FFolioModule& Module = FFolioModule::Get();
	DeviceChanged = Module.OnInputDeviceChanged().AddLambda(
		[this](Folio::EInputDevice) { Refresh(); });
	ConfirmChanged = Module.OnConfirmButtonChanged().AddLambda([this] { Refresh(); });

	Refresh();
}

void FFolioInputModel::SetDocument(Rml::ElementDocument* InDocument)
{
	Document = InDocument;
	Refresh();
}

void FFolioInputModel::Refresh()
{
	const Folio::EInputDevice Device = FFolioModule::Get().GetInputDevice();
	const Folio::EConfirmButton Button = Folio::ConfirmButton();

	if (Document)
	{
		Folio::ApplyInputClasses(Document, Device, Button);
	}

	if (!Handle)
	{
		return;
	}

	DeviceValue = Folio::DeviceName(Device);
	ConfirmValue = Folio::ConfirmName(Button);
	BackValue = Folio::BackName(Button);
	bGamepad = Device == Folio::EInputDevice::Gamepad;

	Handle.DirtyVariable(Folio::InputModel::Device());
	Handle.DirtyVariable(Folio::InputModel::Gamepad());
	Handle.DirtyVariable(Folio::InputModel::Confirm());
	Handle.DirtyVariable(Folio::InputModel::Back());
}

void FFolioInputModel::Release()
{
	// The module can already be gone: a canvas parked in a window is destroyed during static
	// teardown, after ShutdownModule has run.
	if (FFolioModule::IsAvailable())
	{
		FFolioModule& Module = FFolioModule::Get();
		Module.OnInputDeviceChanged().Remove(DeviceChanged);
		Module.OnConfirmButtonChanged().Remove(ConfirmChanged);
	}

	DeviceChanged.Reset();
	ConfirmChanged.Reset();

	// The model itself belongs to the context, which destroys it.
	Handle = Rml::DataModelHandle();
	Document = nullptr;
}
