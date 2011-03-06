/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Dialogs/Internal.hpp"
#include "Protection.hpp"
#include "InfoBoxes/InfoBoxManager.hpp"
#include "Blackboard.hpp"
#include "SettingsAirspace.hpp"
#include "SettingsComputer.hpp"
#include "SettingsMap.hpp"
#include "Appearance.hpp"
#include "LocalPath.hpp"
#include "Profile/ProfileKeys.hpp"
#include "Profile/DisplayConfig.hpp"
#include "Profile/DeviceConfig.hpp"
#include "Logger/Logger.hpp"
#include "Device/Register.hpp"
#include "Device/List.hpp"
#include "Device/Descriptor.hpp"
#include "Screen/Busy.hpp"
#include "Screen/Blank.hpp"
#include "Screen/Key.h"
#include "Form/CheckBox.hpp"
#include "Screen/Layout.hpp"
#include "MainWindow.hpp"
#include "Profile/Profile.hpp"
#include "LocalTime.hpp"
#include "Math/FastMath.h"
#include "Polar/PolarStore.hpp"
#include "Polar/PolarGlue.hpp"
#include "Polar/Polar.hpp"
#include "DataField/Boolean.hpp"
#include "DataField/Enum.hpp"
#include "DataField/Float.hpp"
#include "DataField/FileReader.hpp"
#include "DataField/ComboList.hpp"
#include "Asset.hpp"
#include "GlideRatio.hpp"
#include "Components.hpp"
#include "Task/ProtectedTaskManager.hpp"
#include "Task/Tasks/OrderedTask.hpp"
#include "Dialogs/dlgTaskHelpers.hpp"
#include "Task/Factory/AbstractTaskFactory.hpp"
#include "WayPointFile.hpp"
#include "StringUtil.hpp"
#include "Simulator.hpp"
#include "Compiler.h"
#include "Screen/Graphics.hpp"
#include "InfoBoxes/InfoBoxLayout.hpp"
#include "Hardware/Display.hpp"
#include "OS/PathName.hpp"
#include "Gauge/GlueGaugeVario.hpp"
#include "UnitsFormatter.hpp"
#include "UnitsStore.hpp"
#include "LogFile.hpp"
#include "LanguageGlue.hpp"
#include "PagesConfigPanel.hpp"

#ifdef ANDROID
#include "Android/BluetoothHelper.hpp"
#endif

#include <assert.h>

enum config_page {
  PAGE_SITE,
  PAGE_AIRSPACE,
  PAGE_MAP,
  PAGE_WAYPOINTS,
  PAGE_SYMBOLS,
  PAGE_TERRAIN,
  PAGE_GLIDE_COMPUTER,
  PAGE_SAFETY_FACTORS,
  PAGE_ROUTE,
  PAGE_POLAR,
  PAGE_DEVICES,
  PAGE_UNITS,
  PAGE_INTERFACE,
  PAGE_LAYOUT,
  PAGE_VARIO,
  PAGE_TASK_RULES,
  PAGE_TASK_DEFAULTS,
  PAGE_INFOBOXES,
  PAGE_LOGGER,
  PAGE_PAGES,
  PAGE_EXPERIMENTAL,
};

static const TCHAR *const captions[] = {
  N_("Site"),
  N_("Airspace"),
  N_("Map Display"),
  N_("Waypoint Display"),
  N_("Symbols"),
  N_("Terrain Display"),
  N_("Glide Computer"),
  N_("Safety factors"),
  N_("Route"),
  N_("Polar"),
  N_("Devices"),
  N_("Units"),
  N_("Interface"),
  N_("Layout"),
  N_("FLARM and other gauges"),
  N_("Default task rules"),
  N_("Default task turnpoints"),
  N_("InfoBoxes"),
  N_("Logger"),
  N_("Pages"),
  N_("Experimental features"),
};

static const struct {
  enum DeviceConfig::port_type type;
  const TCHAR *label;
} port_types[] = {
#ifdef _WIN32_WCE
  { DeviceConfig::AUTO, N_("GPS Intermediate Driver") },
#endif
#ifdef ANDROID
  { DeviceConfig::INTERNAL, N_("Built-in GPS") },
#endif
  { DeviceConfig::SERIAL, NULL } /* sentinel */
};

static const unsigned num_port_types =
  sizeof(port_types) / sizeof(port_types[0]) - 1;

static bool loading = false;
static bool changed = false;
static bool taskchanged = false;
static bool requirerestart = false;
static bool waypointneedsave = false;
static config_page current_page;
static WndForm *wf = NULL;
TabbedControl *configuration_tabbed;
static WndButton *buttonPilotName = NULL;
static WndButton *buttonAircraftType = NULL;
static WndButton *buttonAircraftRego = NULL;
static WndButton *buttonLoggerID = NULL;
static WndButton *buttonFonts = NULL;
static WndButton *buttonWaypoints = NULL;
static WndButton *buttonPolarList = NULL;
static WndButton *buttonPolarImport = NULL;
static WndButton *buttonPolarExport = NULL;

static void
UpdateButtons(void)
{
  TCHAR text[120];
  TCHAR val[100];
  if (buttonPilotName) {
    Profile::Get(szProfilePilotName, val, 100);
    if (string_is_empty(val))
      _tcscpy(val, _("(blank)"));

    _stprintf(text, _T("%s: %s"), _("Pilot name"), val);
    buttonPilotName->SetCaption(text);
  }
  if (buttonAircraftType) {
    Profile::Get(szProfileAircraftType, val, 100);
    if (string_is_empty(val))
      _tcscpy(val, _("(blank)"));

    _stprintf(text, _T("%s: %s"), _("Aircraft type"), val);
    buttonAircraftType->SetCaption(text);
  }
  if (buttonAircraftRego) {
    Profile::Get(szProfileAircraftRego, val, 100);
    if (string_is_empty(val))
      _tcscpy(val, _("(blank)"));

    _stprintf(text, _T("%s: %s"), _("Competition ID"), val);
    buttonAircraftRego->SetCaption(text);
  }
  if (buttonLoggerID) {
    Profile::Get(szProfileLoggerID, val, 100);
    if (string_is_empty(val))
      _tcscpy(val, _("(blank)"));

    _stprintf(text, _T("%s: %s"), _("Logger ID"), val);
    buttonLoggerID->SetCaption(text);
  }
}

static void
PageSwitched()
{
  current_page = (config_page)configuration_tabbed->GetCurrentPage();

  TCHAR caption[64];
  _sntprintf(caption, 64, _T("%u %s"),
             (unsigned)current_page + 1,
             gettext(captions[(unsigned)current_page]));
  wf->SetCaption(caption);

  if (buttonFonts != NULL)
    buttonFonts->set_visible(current_page == PAGE_INTERFACE);

  if (buttonWaypoints != NULL)
    buttonWaypoints->set_visible(current_page == PAGE_SITE);

  if (buttonPolarList != NULL)
    buttonPolarList->set_visible(current_page == PAGE_POLAR);

  if (buttonPolarImport != NULL)
    buttonPolarImport->set_visible(current_page == PAGE_POLAR);

  if (buttonPolarExport != NULL)
    buttonPolarExport->set_visible(current_page == PAGE_POLAR);
}

static void
SetupDevice(DeviceDescriptor &device)
{
#ifdef ToDo
  device.DoSetup();
  wf->FocusNext(NULL);
#endif

  // this is a hack, devices dont jet support device dependant setup dialogs

  if (!device.IsVega())
    return;

  changed = dlgConfigurationVarioShowModal();
}

static void
OnSetupDeviceAClicked(WndButton &button)
{
  SetupDevice(DeviceList[0]);

  // this is a hack to get the dialog to retain focus because
  // the progress dialog in the vario configuration somehow causes
  // focus problems
  button.set_focus();
}


static void
OnSetupDeviceBClicked(WndButton &button)
{
  SetupDevice(DeviceList[1]);

  // this is a hack to get the dialog to retain focus because
  // the progress dialog in the vario configuration somehow causes
  // focus problems
  button.set_focus();
}

static void
UpdateDeviceSetupButton(unsigned DeviceIdx, const TCHAR *Name)
{
  assert(DeviceIdx < 26);

  TCHAR button_name[] = _T("cmdSetupDeviceA");
  button_name[(sizeof(button_name) / sizeof(button_name[0])) - 2] += DeviceIdx;

  WndButton *wb = (WndButton *)wf->FindByName(button_name);
  if (wb != NULL)
    wb->set_visible(Name != NULL && _tcscmp(Name, _T("Vega")) == 0);
}

static void
OnUserLevel(CheckBoxControl &control)
{
  changed = true;
  Profile::Set(szProfileUserLevel, control.get_checked());
  wf->FilterAdvanced(control.get_checked());
}

static void
OnDeviceAData(DataField *Sender, DataField::DataAccessKind_t Mode)
{
  switch (Mode) {
  case DataField::daChange:
    UpdateDeviceSetupButton(0, Sender->GetAsString());
    break;

  case DataField::daInc:
  case DataField::daDec:
  case DataField::daSpecial:
    return;
  }
}

static void
OnDeviceBData(DataField *Sender, DataField::DataAccessKind_t Mode)
{
  switch (Mode) {
  case DataField::daChange:
    UpdateDeviceSetupButton(1, Sender->GetAsString());
    break;

  case DataField::daInc:
  case DataField::daDec:
  case DataField::daSpecial:
    return;
  }
}

static void
OnAircraftRegoClicked(gcc_unused WndButton &button)
{
  TCHAR Temp[100];
  if (buttonAircraftRego) {
    Profile::Get(szProfileAircraftRego, Temp, 100);
    if (dlgTextEntryShowModal(Temp, 100)) {
      Profile::Set(szProfileAircraftRego, Temp);
      changed = true;
    }
  }
  UpdateButtons();
}

static void
OnAircraftTypeClicked(gcc_unused WndButton &button)
{
  TCHAR Temp[100];
  if (buttonAircraftType) {
    Profile::Get(szProfileAircraftType, Temp, 100);
    if (dlgTextEntryShowModal(Temp, 100)) {
      Profile::Set(szProfileAircraftType, Temp);
      changed = true;
    }
  }
  UpdateButtons();
}

static void
OnPilotNameClicked(gcc_unused WndButton &button)
{
  TCHAR Temp[100];
  if (buttonPilotName) {
    Profile::Get(szProfilePilotName, Temp, 100);
    if (dlgTextEntryShowModal(Temp, 100)) {
      Profile::Set(szProfilePilotName, Temp);
      changed = true;
    }
  }
  UpdateButtons();
}

static void
OnLoggerIDClicked(gcc_unused WndButton &button)
{
  TCHAR Temp[100];
  if (buttonLoggerID) {
    Profile::Get(szProfileLoggerID, Temp, 100);
    if (dlgTextEntryShowModal(Temp, 100)) {
      Profile::Set(szProfileLoggerID, Temp);
      changed = true;
    }
    ReadAssetNumber();
  }
  UpdateButtons();
}

static void
OnAirspaceColoursClicked(gcc_unused WndButton &button)
{
  dlgAirspaceShowModal(true);
}

static void
OnAirspaceModeClicked(gcc_unused WndButton &button)
{
  dlgAirspaceShowModal(false);
}

static void
OnNextClicked(gcc_unused WndButton &button)
{
  configuration_tabbed->NextPage();
  PageSwitched();
}

static void
OnPrevClicked(gcc_unused WndButton &button)
{
  configuration_tabbed->PreviousPage();
  PageSwitched();
}

static void
OnCloseClicked(gcc_unused WndButton &button)
{
  wf->SetModalResult(mrOK);
}

static void
OnInfoBoxesCircling(gcc_unused WndButton &button)
{
  dlgConfigInfoboxesShowModal(InfoBoxManager::PANEL_CIRCLING);
}

static void
OnInfoBoxesCruise(gcc_unused WndButton &button)
{
  dlgConfigInfoboxesShowModal(InfoBoxManager::PANEL_CRUISE);
}

static void
OnInfoBoxesFinalGlide(gcc_unused WndButton &button)
{
  dlgConfigInfoboxesShowModal(InfoBoxManager::PANEL_FINAL_GLIDE);
}

static void
OnInfoBoxesAuxiliary(gcc_unused WndButton &button)
{
  dlgConfigInfoboxesShowModal(InfoBoxManager::PANEL_AUXILIARY);
}

static void
OnFonts(gcc_unused WndButton &button)
{
  dlgConfigFontsShowModal();
}

static void
OnWaypoints(gcc_unused WndButton &button)
{
  dlgConfigWaypointsShowModal();
}

static void
UpdatePolarFields(const SimplePolar &polar)
{
  LoadFormProperty(*wf, _T("prpPolarV1"), fixed(polar.v1));
  LoadFormProperty(*wf, _T("prpPolarV2"), fixed(polar.v2));
  LoadFormProperty(*wf, _T("prpPolarV3"), fixed(polar.v3));

  LoadFormProperty(*wf, _T("prpPolarW1"), fixed(polar.w1));
  LoadFormProperty(*wf, _T("prpPolarW2"), fixed(polar.w2));
  LoadFormProperty(*wf, _T("prpPolarW3"), fixed(polar.w3));

  LoadFormProperty(*wf, _T("prpPolarMassDry"), fixed(polar.dry_mass));
  LoadFormProperty(*wf, _T("prpPolarMaxBallast"), fixed(polar.max_ballast));

  LoadFormProperty(*wf, _T("prpPolarWingArea"), fixed(polar.wing_area));
}

static void
SetPolarTitle(const TCHAR* title)
{
  TCHAR caption[255];
  _tcscpy(caption,  _("Polar"));
  _tcscat(caption, _T(": "));
  _tcscat(caption, title);
  ((WndFrame *)wf->FindByName(_T("lblPolar")))->SetCaption(caption);
}

static void
UpdatePolarTitle()
{
  TCHAR title[255];
  if (Profile::Get(szProfilePolarName, title, 255))
    SetPolarTitle(title);
}

static bool
SaveFormToPolar(SimplePolar &polar)
{
  bool changed = SaveFormProperty(*wf, _T("prpPolarV1"), polar.v1);
  changed |= SaveFormProperty(*wf, _T("prpPolarV2"), polar.v2);
  changed |= SaveFormProperty(*wf, _T("prpPolarV3"), polar.v3);

  changed |= SaveFormProperty(*wf, _T("prpPolarW1"), polar.w1);
  changed |= SaveFormProperty(*wf, _T("prpPolarW2"), polar.w2);
  changed |= SaveFormProperty(*wf, _T("prpPolarW3"), polar.w3);

  changed |= SaveFormProperty(*wf, _T("prpPolarMassDry"), polar.dry_mass);
  changed |= SaveFormProperty(*wf, _T("prpPolarMaxBallast"), polar.max_ballast);

  changed |= SaveFormProperty(*wf, _T("prpPolarWingArea"), polar.wing_area);

  return changed;
}

static void
UpdatePolarInvalidLabel()
{
  SimplePolar polar;
  SaveFormToPolar(polar);
  if (polar.IsValid())
    ((WndFrame *)wf->FindByName(_T("lblPolarInvalid")))->hide();
  else
    ((WndFrame *)wf->FindByName(_T("lblPolarInvalid")))->show();
}

static void
OnPolarLoadInteral(WndButton &button)
{
  /* create a fake WndProperty for dlgComboPicker() */
  /* XXX reimplement properly */

  DataFieldEnum *dfe = new DataFieldEnum(NULL);
  unsigned len = PolarStore::Count();
  for (unsigned i = 0; i < len; i++)
    dfe->addEnumText(PolarStore::GetName(i), i);

  dfe->Sort();
  ComboList *list = dfe->CreateComboList();

  /* let the user select */

  int result = ComboPicker(XCSoarInterface::main_window, _("Load Polar"), *list, NULL);
  if (result >= 0) {
    SimplePolar polar;
    PolarStore::Read(dfe->getItem(result), polar);
    UpdatePolarFields(polar);

    Profile::Set(szProfilePolarName, PolarStore::GetName(dfe->getItem(result)));
    UpdatePolarTitle();
    UpdatePolarInvalidLabel();
  }

  delete dfe;
  delete list;
}

static void
OnPolarLoadFromFile(WndButton &button)
{
  /* create a fake WndProperty for dlgComboPicker() */
  /* XXX reimplement properly */

  DataFieldFileReader *dfe = new DataFieldFileReader(NULL);
  dfe->ScanDirectoryTop(_T("*.plr"));

  ComboList *list = dfe->CreateComboList();

  /* let the user select */

  int result = ComboPicker(XCSoarInterface::main_window,
                           _("Load Polar from file"), *list, NULL);
  if (result > 0) {
    const TCHAR* path = dfe->getItem(result);
    SimplePolar polar;
    PolarGlue::LoadFromFile(polar, path);
    UpdatePolarFields(polar);

    Profile::Set(szProfilePolarName, BaseName(path));
    UpdatePolarTitle();
    UpdatePolarInvalidLabel();
  }

  delete dfe;
  delete list;
}

static void
OnPolarExport(WndButton &button)
{
  TCHAR filename[69] = _T("");
  if (!dlgTextEntryShowModal(filename, 64))
    return;

  TCHAR path[MAX_PATH];
  _tcscat(filename, _T(".plr"));
  LocalPath(path, filename);

  SimplePolar polar;
  SaveFormToPolar(polar);
  if (PolarGlue::SaveToFile(polar, path)) {
    Profile::Set(szProfilePolarName, filename);
    UpdatePolarTitle();
  }
}

static void
OnPolarFieldData(DataField *Sender, DataField::DataAccessKind_t Mode)
{
  switch (Mode) {
  case DataField::daChange:
    Profile::Set(szProfilePolarName, _T("Custom"));
    UpdatePolarTitle();
    UpdatePolarInvalidLabel();
    break;

  case DataField::daInc:
  case DataField::daDec:
  case DataField::daSpecial:
    return;
  }
}

static void
UpdateUnitFields(const UnitSetting &units)
{
  unsigned index;
  index = (units.SpeedUnit == unStatuteMilesPerHour) ? 0 :
          (units.SpeedUnit == unKnots) ? 1 : 2;
  LoadFormProperty(*wf, _T("prpUnitsSpeed"), index);

  index = (units.TaskSpeedUnit == unStatuteMilesPerHour) ? 0 :
          (units.TaskSpeedUnit == unKnots) ? 1 : 2;
  LoadFormProperty(*wf, _T("prpUnitsTaskSpeed"), index);

  index = (units.DistanceUnit == unStatuteMiles) ? 0 :
          (units.DistanceUnit == unNauticalMiles) ? 1 : 2;
  LoadFormProperty(*wf, _T("prpUnitsDistance"), index);

  index = (units.AltitudeUnit == unFeet) ? 0 : 1;
  LoadFormProperty(*wf, _T("prpUnitsAltitude"), index);

  index = (units.TemperatureUnit == unGradFahrenheit) ? 1 : 0;
  LoadFormProperty(*wf, _T("prpUnitsTemperature"), index);

  index = (units.VerticalSpeedUnit == unKnots) ? 0 :
          (units.VerticalSpeedUnit == unFeetPerMinute) ? 2 : 1;
  LoadFormProperty(*wf, _T("prpUnitsLift"), index);
}

static void
SetUnitsTitle(const TCHAR* title)
{
  TCHAR caption[255];
  _tcscpy(caption,  _("Units"));
  _tcscat(caption, _T(": "));
  _tcscat(caption, title);
  ((WndFrame *)wf->FindByName(_T("lblUnitsSetting")))->SetCaption(caption);
}

static void
UpdateUnitsTitle()
{
  TCHAR title[255];
  if (Profile::Get(szProfileUnitsPresetName, title, 255))
    SetUnitsTitle(title);
}

static void
OnLoadUnitsPreset(WndButton &button)
{
  /* create a fake WndProperty for dlgComboPicker() */
  /* XXX reimplement properly */

  DataFieldEnum *dfe = new DataFieldEnum(NULL);
  unsigned len = Units::Store::Count();
  for (unsigned i = 0; i < len; i++)
    dfe->addEnumText(Units::Store::GetName(i), i);

  dfe->Sort();
  ComboList *list = dfe->CreateComboList();

  /* let the user select */

  int result = ComboPicker(XCSoarInterface::main_window, _("Unit presets"), *list, NULL);
  if (result >= 0) {
    const UnitSetting& units = Units::Store::Read(dfe->getItem(result));
    UpdateUnitFields(units);

    Profile::Set(szProfileUnitsPresetName, Units::Store::GetName(dfe->getItem(result)));
    UpdateUnitsTitle();
  }

  delete dfe;
  delete list;
}

static void
OnUnitFieldData(DataField *Sender, DataField::DataAccessKind_t Mode)
{
  switch (Mode) {
  case DataField::daChange:
    if (!loading)
      Profile::Set(szProfileUnitsPresetName, _T("Custom"));
    UpdateUnitsTitle();
    break;

  case DataField::daInc:
  case DataField::daDec:
  case DataField::daSpecial:
    return;
  }
}

static bool
FormKeyDown(WndForm &Sender, unsigned key_code)
{
  (void)Sender;

  switch (key_code) {
  case VK_LEFT:
#ifdef GNAV
  case '6':
#endif
    ((WndButton *)wf->FindByName(_T("cmdPrev")))->set_focus();
    configuration_tabbed->PreviousPage();
    PageSwitched();
    return true;

  case VK_RIGHT:
#ifdef GNAV
  case '7':
#endif
    ((WndButton *)wf->FindByName(_T("cmdNext")))->set_focus();
    configuration_tabbed->NextPage();
    PageSwitched();
    return true;

  default:
    return false;
  }
}

static void
SetLocalTime(void)
{
  WndProperty* wp;
  TCHAR temp[20];
  Units::TimeToText(temp, TimeLocal(XCSoarInterface::Basic().Time));

  wp = (WndProperty*)wf->FindByName(_T("prpLocalTime"));
  assert(wp != NULL);

  wp->SetText(temp);
  wp->RefreshDisplay();
}

static void
OnUTCData(DataField *Sender, DataField::DataAccessKind_t Mode)
{
  switch(Mode) {
  case DataField::daChange:
  {
    DataFieldFloat &df = *(DataFieldFloat *)Sender;
    int ival = iround(df.GetAsFixed() * 3600);
    if (XCSoarInterface::SettingsComputer().UTCOffset != ival)
      XCSoarInterface::SetSettingsComputer().UTCOffset = ival;

    SetLocalTime();
    break;
  }
  case DataField::daInc:
  case DataField::daDec:
  case DataField::daSpecial:
    return;
  }
}

extern void OnInfoBoxHelp(WindowControl * Sender);

static CallBackTableEntry CallBackTable[] = {
  DeclareCallBackEntry(OnAirspaceColoursClicked),
  DeclareCallBackEntry(OnAirspaceModeClicked),
  DeclareCallBackEntry(OnUTCData),
  DeclareCallBackEntry(OnNextClicked),
  DeclareCallBackEntry(OnPrevClicked),
  DeclareCallBackEntry(OnSetupDeviceAClicked),
  DeclareCallBackEntry(OnSetupDeviceBClicked),
  DeclareCallBackEntry(OnInfoBoxHelp),
  DeclareCallBackEntry(OnDeviceAData),
  DeclareCallBackEntry(OnDeviceBData),
  DeclareCallBackEntry(OnPolarLoadInteral),
  DeclareCallBackEntry(OnPolarLoadFromFile),
  DeclareCallBackEntry(OnPolarExport),
  DeclareCallBackEntry(OnPolarFieldData),
  DeclareCallBackEntry(OnLoadUnitsPreset),
  DeclareCallBackEntry(OnUnitFieldData),
  DeclareCallBackEntry(OnUserLevel),
  DeclareCallBackEntry(NULL)
};

static DeviceConfig device_config[NUMDEV];
static int SpeedUnits = 1; // default is knots
static int TaskSpeedUnits = 2; // default is kph
static int DistanceUnits = 2; // default is km
static int LiftUnits = 0;
static int AltitudeUnits = 0; //default ft
static int TemperatureUnits = 0; //default is celcius

static void
InitFileField(WndProperty &wp, const TCHAR *profile_key, const TCHAR *filters)
{
  DataFieldFileReader &df = *(DataFieldFileReader *)wp.GetDataField();

  size_t length;
  while ((length = _tcslen(filters)) > 0) {
    df.ScanDirectoryTop(filters);
    filters += length + 1;
  }

  TCHAR path[MAX_PATH];
  if (Profile::GetPath(profile_key, path))
    df.Lookup(path);

  wp.RefreshDisplay();
}

static void
InitFileField(WndForm &wf, const TCHAR *control_name,
              const TCHAR *profile_key, const TCHAR *filters)
{
  WndProperty *wp = (WndProperty *)wf.FindByName(control_name);
  assert(wp != NULL);

  InitFileField(*wp, profile_key, filters);
}

static void
SetupDeviceFields(const DeviceDescriptor &device, const DeviceConfig &config,
                  WndProperty *port_field, WndProperty *speed_field,
                  WndProperty *driver_field, WndButton *setup_button)
{
#ifndef ANDROID
  static const TCHAR *const COMMPort[] = {
    _T("COM1"), _T("COM2"), _T("COM3"), _T("COM4"),
    _T("COM5"), _T("COM6"), _T("COM7"), _T("COM8"),
    _T("COM9"), _T("COM10"), _T("COM0"),
    NULL
  };

  static const TCHAR *const tSpeed[] = {
    _T("1200"), _T("2400"), _T("4800"), _T("9600"),
    _T("19200"), _T("38400"), _T("57600"), _T("115200"),
    NULL
  };
#endif

  if (port_field != NULL) {
    DataFieldEnum *dfe = (DataFieldEnum *)port_field->GetDataField();

    for (unsigned i = 0; port_types[i].label != NULL; i++) {
      dfe->addEnumText(gettext(port_types[i].label));

      if (port_types[i].type == config.port_type)
        dfe->Set(i);
    }

#ifdef ANDROID
    JNIEnv *env = Java::GetEnv();
    jobjectArray bonded = BluetoothHelper::list(env);
    if (bonded != NULL) {
      jsize n = env->GetArrayLength(bonded) / 2;
      for (jsize i = 0; i < n; ++i) {
        jstring address = (jstring)env->GetObjectArrayElement(bonded, i * 2);
        if (address == NULL)
          continue;

        const char *address2 = env->GetStringUTFChars(address, NULL);
        if (address2 == NULL)
          continue;

        jstring name = (jstring)env->GetObjectArrayElement(bonded, i * 2 + 1);
        const char *name2 = name != NULL
          ? env->GetStringUTFChars(name, NULL)
          : NULL;

        dfe->addEnumText(address2, name2);
        env->ReleaseStringUTFChars(address, address2);
        if (name2 != NULL)
          env->ReleaseStringUTFChars(name, name2);
      }

      env->DeleteLocalRef(bonded);

      if (config.port_type == DeviceConfig::RFCOMM &&
          !config.bluetooth_mac.empty()) {
        if (!dfe->Exists(config.bluetooth_mac))
          dfe->addEnumText(config.bluetooth_mac);
        dfe->SetAsString(config.bluetooth_mac);
      }
    }
#else
    dfe->addEnumTexts(COMMPort);

    switch (config.port_type) {
    case DeviceConfig::SERIAL:
      dfe->Set(config.port_index + num_port_types);
      break;

    case DeviceConfig::RFCOMM:
    case DeviceConfig::AUTO:
    case DeviceConfig::INTERNAL:
      break;
    }
#endif

    port_field->RefreshDisplay();
  }

  if (speed_field != NULL) {
#ifdef ANDROID
    speed_field->hide();
#else
    DataFieldEnum *dfe = (DataFieldEnum *)speed_field->GetDataField();
    dfe->addEnumTexts(tSpeed);

    dfe->Set(config.speed_index);
    speed_field->RefreshDisplay();
#endif
  }

  if (driver_field) {
    DataFieldEnum *dfe = (DataFieldEnum *)driver_field->GetDataField();

    const TCHAR *DeviceName;
    for (unsigned i = 0; (DeviceName = devRegisterGetName(i)) != NULL; i++)
      dfe->addEnumText(DeviceName);

    dfe->Sort(1);
    dfe->SetAsString(config.driver_name);

    driver_field->RefreshDisplay();
  }

  if (setup_button != NULL)
    setup_button->set_visible(device.IsVega());
}

static void
setVariables()
{
  WndProperty *wp;

  buttonPilotName = ((WndButton *)wf->FindByName(_T("cmdPilotName")));
  if (buttonPilotName)
    buttonPilotName->SetOnClickNotify(OnPilotNameClicked);

  buttonAircraftType = ((WndButton *)wf->FindByName(_T("cmdAircraftType")));
  if (buttonAircraftType)
    buttonAircraftType->SetOnClickNotify(OnAircraftTypeClicked);

  buttonAircraftRego = ((WndButton *)wf->FindByName(_T("cmdAircraftRego")));
  if (buttonAircraftRego)
    buttonAircraftRego->SetOnClickNotify(OnAircraftRegoClicked);

  buttonLoggerID = ((WndButton *)wf->FindByName(_T("cmdLoggerID")));
  if (buttonLoggerID)
    buttonLoggerID->SetOnClickNotify(OnLoggerIDClicked);

  buttonFonts = ((WndButton *)wf->FindByName(_T("cmdFonts")));
  if (buttonFonts)
    buttonFonts->SetOnClickNotify(OnFonts);

  buttonWaypoints = ((WndButton *)wf->FindByName(_T("cmdWaypoints")));
  if (buttonWaypoints)
    buttonWaypoints->SetOnClickNotify(OnWaypoints);

  buttonPolarList = ((WndButton *)wf->FindByName(_T("cmdLoadInternalPolar")));
  buttonPolarImport = ((WndButton *)wf->FindByName(_T("cmdLoadPolarFile")));
  buttonPolarExport = ((WndButton *)wf->FindByName(_T("cmdSavePolarFile")));

  UpdateButtons();

  for (unsigned i = 0; i < NUMDEV; ++i)
    Profile::GetDeviceConfig(i, device_config[i]);

  SetupDeviceFields(DeviceList[0], device_config[0],
                    (WndProperty*)wf->FindByName(_T("prpComPort1")),
                    (WndProperty*)wf->FindByName(_T("prpComSpeed1")),
                    (WndProperty*)wf->FindByName(_T("prpComDevice1")),
                    (WndButton *)wf->FindByName(_T("cmdSetupDeviceA")));

  SetupDeviceFields(DeviceList[1], device_config[1],
                    (WndProperty*)wf->FindByName(_T("prpComPort2")),
                    (WndProperty*)wf->FindByName(_T("prpComSpeed2")),
                    (WndProperty*)wf->FindByName(_T("prpComDevice2")),
                    (WndButton *)wf->FindByName(_T("cmdSetupDeviceB")));

  const SETTINGS_COMPUTER &settings_computer =
    XCSoarInterface::SettingsComputer();

  const SETTINGS_MAP &settings_map =
    XCSoarInterface::SettingsMap();

  wp = (WndProperty*)wf->FindByName(_T("prpAirspaceDisplay"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("All on"));
    dfe->addEnumText(_("Clip"));
    dfe->addEnumText(_("Auto"));
    dfe->addEnumText(_("All below"));
    dfe->Set(settings_computer.AltitudeMode);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpUTCOffset"),
                   fixed(iround(fixed(settings_computer.UTCOffset) / 1800)) / 2);

#ifdef WIN32
  if (is_embedded() && !is_altair())
    ((WndProperty*)wf->FindByName(_T("prpUTCOffset")))->set_enabled(false);
#endif

  SetLocalTime();

  LoadFormProperty(*wf, _T("prpClipAltitude"), ugAltitude,
                   settings_computer.ClipAltitude);
  LoadFormProperty(*wf, _T("prpAltWarningMargin"), ugAltitude,
                   settings_computer.airspace_warnings.AltWarningMargin);

  LoadFormProperty(*wf, _T("prpAirspaceOutline"),
                   settings_map.bAirspaceBlackOutline);

  wp = (WndProperty *)wf->FindByName(_T("prpAirspaceFillMode"));
  {
#ifdef ENABLE_OPENGL
    wp->hide();
#else
    DataFieldEnum &dfe = *(DataFieldEnum *)wp->GetDataField();
    dfe.addEnumText(_("Default"), SETTINGS_MAP::AS_FILL_DEFAULT);
    dfe.addEnumText(_("Fill all"), SETTINGS_MAP::AS_FILL_ALL);
    dfe.addEnumText(_("Fill padding"), SETTINGS_MAP::AS_FILL_PADDING);
    dfe.Set(settings_map.AirspaceFillMode);
    wp->RefreshDisplay();
#endif
  }

  LoadFormProperty(*wf, _T("prpLoggerShortName"),
                   settings_computer.LoggerShortName);
  LoadFormProperty(*wf, _T("prpDebounceTimeout"),
                   XCSoarInterface::debounceTimeout);

  LoadFormProperty(*wf, _T("prpEnableFLARMMap"),
                   XCSoarInterface::SettingsMap().EnableFLARMMap);

  LoadFormProperty(*wf, _T("prpEnableFLARMGauge"),
                   XCSoarInterface::SettingsMap().EnableFLARMGauge);
  LoadFormProperty(*wf, _T("prpAutoCloseFlarmDialog"),
                   XCSoarInterface::SettingsMap().AutoCloseFlarmDialog);
  LoadFormProperty(*wf, _T("prpEnableTAGauge"),
                   XCSoarInterface::SettingsMap().EnableTAGauge);
  LoadFormProperty(*wf, _T("prpAirspaceWarnings"),
                   settings_computer.EnableAirspaceWarnings);
  LoadFormProperty(*wf, _T("prpWarningTime"),
                   settings_computer.airspace_warnings.WarningTime);
  LoadFormProperty(*wf, _T("prpAcknowledgementTime"),
                   settings_computer.airspace_warnings.AcknowledgementTime);

  wp = (WndProperty*)wf->FindByName(_T("prpWaypointLabels"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);
    dfe->addEnumText(_("Full Name"), DISPLAYNAME, _("The full name of each waypoint is displayed."));
    dfe->addEnumText(_("First Word of Name"), DISPLAYUNTILSPACE, _("The first word of the waypoint name is displayed."));
    dfe->addEnumText(_("First 3 Letters"), DISPLAYFIRSTTHREE, _("The first 3 letters of the waypoint name are displayed."));
    dfe->addEnumText(_("First 5 Letters"), DISPLAYFIRSTFIVE, _("The first 5 letters of the waypoint name are displayed."));
    dfe->addEnumText(_("None"), DISPLAYNONE, _("No waypoint name is displayed."));
    dfe->Set(XCSoarInterface::SettingsMap().DisplayTextType);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpWaypointLabelStyle"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Rounded Rectangle"), RoundedBlack);
    dfe->addEnumText(_("Outlined"), OutlinedInverted);
    dfe->Set(XCSoarInterface::SettingsMap().LandableRenderMode);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpWayPointLabelSelection"));
  if (wp) {

    //Determines what waypoint labels are displayed for each waypoint (space permitting):&#10;
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);
    dfe->addEnumText(_("All"), wlsAllWayPoints,
                     _("All waypoints labels will displayed."));
    dfe->addEnumText(_("Task Waypoints & Landables"),
                     wlsTaskAndLandableWayPoints,
                     _("All waypoints labels part of a task and all landables will be displayed."));
    dfe->addEnumText(_("Task Waypoints"), wlsTaskWayPoints,
                     _("All waypoints labels part of a task will be displayed."));
    dfe->addEnumText(_("None"), wlsNoWayPoints,
                     _("No waypoint labels will be displayed."));
    dfe->Set(XCSoarInterface::SettingsMap().WayPointLabelSelection);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpEnableTerrain"),
                   XCSoarInterface::SettingsMap().EnableTerrain);
  LoadFormProperty(*wf, _T("prpEnableTopography"),
                   XCSoarInterface::SettingsMap().EnableTopography);

  wp = (WndProperty*)wf->FindByName(_T("prpSlopeShadingType"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Off"));
    dfe->addEnumText(_("Fixed"));
    dfe->addEnumText(_("Sun"));
    dfe->addEnumText(_("Wind"));
    dfe->Set(XCSoarInterface::SettingsMap().SlopeShadingType);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpCirclingZoom"),
                   XCSoarInterface::SettingsMap().CircleZoom);

  LoadFormProperty(*wf, _T("prpMaxAutoZoomDistance"), ugDistance,
                   XCSoarInterface::SettingsMap().MaxAutoZoomDistance);

  wp = (WndProperty*)wf->FindByName(_T("prpOrientationCruise"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);
    dfe->addEnumText(_("Track up"), TRACKUP,
                     _("The moving map display will be rotated so the glider's track is oriented up."));
    dfe->addEnumText(_("North up"), NORTHUP,
                     _("The moving map display will always be orientated north to south and the glider icon will be rotated to show its course."));
    dfe->addEnumText(_("Target up"), TARGETUP,
                     _("The moving map display will be rotated so the navigation target is oriented up."));
    dfe->Set(XCSoarInterface::SettingsMap().OrientationCruise);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpOrientationCircling"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);
    dfe->addEnumText(_("Track up"), TRACKUP,
                     _("The moving map display will be rotated so the glider's track is oriented up."));
    dfe->addEnumText(_("North up"), NORTHUP,
                     _("The moving map display will always be orientated north to south and the glider icon will be rotated to show its course."));
    dfe->addEnumText(_("Target up"), TARGETUP,
                     _("The moving map display will be rotated so the navigation target is oriented up."));
    dfe->Set(XCSoarInterface::SettingsMap().OrientationCircling);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpMapShiftBias"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);
    dfe->addEnumText(_("None"), MAP_SHIFT_BIAS_NONE, _("Disable adjustments."));
    dfe->addEnumText(_("Heading"), MAP_SHIFT_BIAS_HEADING, _("Use a recent average of the flown heading as basis."));
    dfe->addEnumText(_("Target"), MAP_SHIFT_BIAS_TARGET, _("Use the current target waypoint as basis."));
    dfe->Set(XCSoarInterface::SettingsMap().MapShiftBias);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpMenuTimeout"),
                   XCSoarInterface::MenuTimeoutMax / 2);

  LoadFormProperty(*wf, _T("prpSafetyAltitudeArrival"), ugAltitude,
                   settings_computer.safety_height_arrival);
  LoadFormProperty(*wf, _T("prpSafetyAltitudeTerrain"), ugAltitude,
                   settings_computer.route_planner.safety_height_terrain);

  wp = (WndProperty*)wf->FindByName(_T("prpAbortTaskMode"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Simple"));
    dfe->addEnumText(_("Task"));
    dfe->addEnumText(_("Home"));
    dfe->Set(settings_computer.abort_task_mode);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpFinalGlideTerrain"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Off"));
    dfe->addEnumText(_("Line"));
    dfe->addEnumText(_("Shade"));
    dfe->Set(settings_computer.FinalGlideTerrain);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpEnableNavBaroAltitude"),
                   settings_computer.EnableNavBaroAltitude);

  wp = (WndProperty*)wf->FindByName(_T("prpWindArrowStyle"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Arrow head"));
    dfe->addEnumText(_("Full arrow"));
    dfe->Set(XCSoarInterface::SettingsMap().WindArrowStyle);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAutoWind"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Manual"));
    dfe->addEnumText(_("Circling"));
    dfe->addEnumText(_("ZigZag"));
    dfe->addEnumText(_("Both"));
    dfe->Set(settings_computer.AutoWindMode);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpExternalWind"), settings_computer.ExternalWind);

  wp = (WndProperty*)wf->FindByName(_T("prpAutoMcMode"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Final glide"));
    dfe->addEnumText(_("Trending Average climb"));
    dfe->addEnumText(_("Both"));
    dfe->Set((int)settings_computer.auto_mc_mode);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpBlockSTF"),
                   settings_computer.EnableBlockSTF);

  wp = (WndProperty*)wf->FindByName(_T("prpContests"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("OLC FAI"), OLC_FAI);
    dfe->addEnumText(_("OLC Classic"), OLC_Classic);
    dfe->addEnumText(_("OLC League"), OLC_League);
    dfe->addEnumText(_("OLC Plus"), OLC_Plus);
    dfe->addEnumText(_("XContest"), OLC_XContest);
    dfe->addEnumText(_("DHV-XC"), OLC_DHVXC);
    dfe->addEnumText(_("SIS-AT"), OLC_SISAT);
    dfe->Set(settings_computer.contest);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpHandicap"),
                   settings_computer.contest_handicap);

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsSpeed"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Statute"));
    dfe->addEnumText(_("Nautical"));
    dfe->addEnumText(_("Metric"));
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsLatLon"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    const TCHAR *const units_lat_lon[] = {
      _T("DDMMSS"),
      _T("DDMMSS.ss"),
      _T("DDMM.mmm"),
      _T("DD.dddd"),
      NULL
    };

    dfe->addEnumTexts(units_lat_lon);
    dfe->Set(Units::GetCoordinateFormat());
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsTaskSpeed"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Statute"));
    dfe->addEnumText(_("Nautical"));
    dfe->addEnumText(_("Metric"));
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsDistance"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Statute"));
    dfe->addEnumText(_("Nautical"));
    dfe->addEnumText(_("Metric"));
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsAltitude"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Feet"));
    dfe->addEnumText(_("Meters"));
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsTemperature"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("C"));
    dfe->addEnumText(_("F"));
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsLift"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Knots"));
    dfe->addEnumText(_("M/s"));
    dfe->addEnumText(_("ft/min"));
    wp->RefreshDisplay();
  }

  UpdateUnitFields(Units::Current);

  LoadFormProperty(*wf, _T("prpTrailDrift"),
                   XCSoarInterface::SettingsMap().EnableTrailDrift);
  LoadFormProperty(*wf, _T("prpDetourCostMarker"),
                   XCSoarInterface::SettingsMap().EnableDetourCostMarker);
  LoadFormProperty(*wf, _T("prpSetSystemTimeFromGPS"),
                   XCSoarInterface::SettingsMap().SetSystemTimeFromGPS);
  LoadFormProperty(*wf, _T("prpIgnoreNMEAChecksum"),
                   NMEAParser::ignore_checksum);
  LoadFormProperty(*wf, _T("prpAbortSafetyUseCurrent"),
                   settings_computer.safety_mc_use_current);
  LoadFormProperty(*wf, _T("prpDisableAutoLogger"),
                   !settings_computer.DisableAutoLogger);

  LoadFormProperty(*wf, _T("prpSafetyMacCready"), ugVerticalSpeed,
                   settings_computer.safety_mc);

  LoadFormProperty(*wf, _T("prpRiskGamma"), settings_computer.risk_gamma);

  wp = (WndProperty*)wf->FindByName(_T("prpDisplayTrackBearing"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Off"));
    dfe->addEnumText(_("On"));
    dfe->addEnumText(_("Auto"));
    dfe->Set(XCSoarInterface::SettingsMap().DisplayTrackBearing);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpTrail"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Off"));
    dfe->addEnumText(_("Long"));
    dfe->addEnumText(_("Short"));
    dfe->addEnumText(_("Full"));
    dfe->Set(XCSoarInterface::SettingsMap().TrailActive);
    wp->RefreshDisplay();
  }

  PagesConfigPanel::Init(wf);

  SimplePolar polar;
  if (!PolarGlue::LoadFromProfile(polar)) {
    PolarGlue::LoadDefault(polar);
    SetPolarTitle(polar.name);
  }
  UpdatePolarFields(polar);
  UpdatePolarTitle();
  UpdatePolarInvalidLabel();

  LoadFormProperty(*wf, _T("prpMaxManoeuveringSpeed"), ugHorizontalSpeed,
                   settings_computer.SafetySpeed);

  LoadFormProperty(*wf, _T("prpBallastSecsToEmpty"),
                   settings_computer.BallastSecsToEmpty);

  InitFileField(*wf, _T("prpAirspaceFile"),
                szProfileAirspaceFile, _T("*.txt\0*.air\0*.sua\0"));
  InitFileField(*wf, _T("prpAdditionalAirspaceFile"),
                szProfileAdditionalAirspaceFile, _T("*.txt\0*.air\0*.sua\0"));
  InitFileField(*wf, _T("prpWaypointFile"),
                szProfileWayPointFile, _T("*.dat\0*.xcw\0*.cup\0*.wpz\0"));
  InitFileField(*wf, _T("prpAdditionalWaypointFile"),
                szProfileAdditionalWayPointFile,
                _T("*.dat\0*.xcw\0*.cup\0*.wpz\0"));
  InitFileField(*wf, _T("prpMapFile"),
                szProfileMapFile, _T("*.xcm\0*.lkm\0"));
  InitFileField(*wf, _T("prpTerrainFile"),
                szProfileTerrainFile, _T("*.jp2\0"));
  InitFileField(*wf, _T("prpTopographyFile"),
                szProfileTopographyFile, _T("*.tpl\0"));
  InitFileField(*wf, _T("prpAirfieldFile"),
                szProfileAirfieldFile, _T("*.txt\0"));

  wp = (WndProperty *)wf->FindByName(_T("prpLanguageFile"));
  if (wp != NULL) {
    DataFieldEnum &df = *(DataFieldEnum *)wp->GetDataField();
    df.addEnumText(_("Automatic"));
    df.addEnumText(_("None"));

#ifdef HAVE_BUILTIN_LANGUAGES
    for (const struct builtin_language *l = language_table;
         l->resource != NULL; ++l)
      df.addEnumText(l->resource);
#endif

    DataFieldFileReader files(NULL);
    files.ScanDirectoryTop(_T("*.mo"));
    for (unsigned i = 0; i < files.size(); ++i) {
      const TCHAR *path = files.getItem(i);
      if (path == NULL)
        continue;

      path = BaseName(path);
      if (path != NULL && !df.Exists(path))
        df.addEnumText(path);
    }

    df.Sort(2);

    TCHAR value[MAX_PATH];
    if (!Profile::GetPath(szProfileLanguageFile, value))
      value[0] = _T('\0');

    if (_tcscmp(value, _T("none")) == 0)
      df.Set(1);
    else if (!string_is_empty(value) && _tcscmp(value, _T("auto")) != 0) {
      const TCHAR *base = BaseName(value);
      if (base != NULL)
        df.SetAsString(base);
    }

    wp->RefreshDisplay();
  }

  InitFileField(*wf, _T("prpStatusFile"),
                szProfileStatusFile, _T("*.xcs\0"));
  InitFileField(*wf, _T("prpInputFile"),
                szProfileInputFile, _T("*.xci\0"));

  wp = (WndProperty*)wf->FindByName(_T("prpAppStatusMessageAlignment"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Center"));
    dfe->addEnumText(_("Topleft"));
    dfe->Set(Appearance.StateMessageAlign);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpTextInput"));
  assert(wp != NULL);
  if (has_pointer()) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Default"));
    dfe->addEnumText(_("Keyboard"));
    dfe->addEnumText(_("HighScore Style"));
    dfe->Set(Appearance.TextInputStyle);
    wp->RefreshDisplay();
  } else
    /* on-screen keyboard doesn't work without a pointing device
       (mouse or touch screen), hide the option on Altair */
    wp->hide();

  wp = (WndProperty*)wf->FindByName(_T("prpTabDialogStyle"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Text"));
    dfe->addEnumText(_("Icons"));
    dfe->Set(Appearance.DialogTabStyle);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpDialogStyle"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Full width"));
    dfe->addEnumText(_("Scaled"));
    dfe->addEnumText(_("Scaled centered"));
    dfe->addEnumText(_("Fixed"));
    dfe->Set(DialogStyleSetting);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxBorder"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Box"));
    dfe->addEnumText(_("Tab"));
    dfe->Set(Appearance.InfoBoxBorder);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxGeom"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();

    dfe->addEnumText(_("8 Top + Bottom (Portrait)")); // 0
    dfe->addEnumText(_("8 Bottom (Portrait)"));  // 1
    dfe->addEnumText(_("8 Top (Portrait)"));  // 2
    dfe->addEnumText(_("8 Left + Right (Landscape)"));  // 3
    dfe->addEnumText(_("8 Left (Landscape)"));  // 4
    dfe->addEnumText(_("8 Right (Landscape)")); // 5
    dfe->addEnumText(_("9 Right + Vario (Landscape)"));  // 6
    dfe->addEnumText(_("5 Right (Square)")); // 7
    dfe->addEnumText(_("12 Right (Landscape)")); // 8
    dfe->Set(InfoBoxLayout::InfoBoxGeometry);
    wp->RefreshDisplay();
  }

  if (Display::RotateSupported()) {
    wp = (WndProperty*)wf->FindByName(_T("prpDisplayOrientation"));
    assert(wp != NULL);

    DataFieldEnum *dfe = (DataFieldEnum *)wp->GetDataField();
    dfe->addEnumText(_("Default"));
    dfe->addEnumText(_("Portrait"));
    dfe->addEnumText(_("Landscape"));
    dfe->Set(Profile::GetDisplayOrientation());
    wp->RefreshDisplay();
  } else {
    wp = (WndProperty*)wf->FindByName(_T("prpDisplayOrientation"));
    assert(wp != NULL);
    wp->hide();
  }

#if defined(_WIN32_WCE) && !defined(GNAV)
// VENTA-ADDON Model change config menu 11
  wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxModel"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Generic"));
    dfe->addEnumText(_T("HP31x"));
    dfe->addEnumText(_T("MedionP5"));
    dfe->addEnumText(_T("MIO"));
    dfe->addEnumText(_T("Nokia500")); // VENTA3
    dfe->addEnumText(_T("PN6000"));
    dfe->Set((int)GlobalModelType);
    wp->RefreshDisplay();
  }
#endif

  wp = (WndProperty*)wf->FindByName(_T("prpGestures"));
  if (wp) {
    if (has_pointer()) {
      DataFieldBoolean &df = *(DataFieldBoolean *)wp->GetDataField();
      df.Set(settings_computer.EnableGestures);
      wp->RefreshDisplay();
    } else {
      wp->hide();
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAverEffTime"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("15 seconds"));
    dfe->addEnumText(_("30 seconds"));
    dfe->addEnumText(_("60 seconds"));
    dfe->addEnumText(_("90 seconds"));
    dfe->addEnumText(_("2 minutes"));
    dfe->addEnumText(_("3 minutes"));
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->Set(settings_computer.AverEffTime);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAppIndFinalGlide"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Default"));
    dfe->addEnumText(_("Alternate"));
    dfe->Set(Appearance.IndFinalGlide);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAppIndLandable"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Purple Circle"));
    dfe->addEnumText(_("B/W"));
    dfe->addEnumText(_("Orange"));
    dfe->Set(Appearance.IndLandable);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpAppUseSWLandablesRendering"),
                   Appearance.UseSWLandablesRendering);

  LoadFormProperty(*wf, _T("prpAppLandableRenderingScale"),
                   Appearance.LandableRenderingScale);

  LoadFormProperty(*wf, _T("prpAppScaleRunwayLength"),
                   Appearance.ScaleRunwayLength);

  wp = (WndProperty*)wf->FindByName(_T("prpEnableExternalTriggerCruise"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Off"));
    dfe->addEnumText(_("Flap"));
    dfe->addEnumText(_("SC"));
    dfe->Set(settings_computer.EnableExternalTriggerCruise);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpAppInverseInfoBox"),
                   Appearance.InverseInfoBox);
  LoadFormProperty(*wf, _T("prpGliderScreenPosition"),
                   XCSoarInterface::SettingsMap().GliderScreenPosition);
  LoadFormProperty(*wf, _T("prpTerrainContrast"),
                   XCSoarInterface::SettingsMap().TerrainContrast * 100 / 255);
  LoadFormProperty(*wf, _T("prpTerrainBrightness"),
                   XCSoarInterface::SettingsMap().TerrainBrightness * 100 / 255);

  wp = (WndProperty*)wf->FindByName(_T("prpTerrainRamp"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("Low lands"));
    dfe->addEnumText(_("Mountainous"));
    dfe->addEnumText(_("Imhof 7"));
    dfe->addEnumText(_("Imhof 4"));
    dfe->addEnumText(_("Imhof 12"));
    dfe->addEnumText(_("Imhof Atlas"));
    dfe->addEnumText(_("ICAO"));
    dfe->addEnumText(_("Grey"));
    dfe->Set(XCSoarInterface::SettingsMap().TerrainRamp);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpAppInfoBoxColors"), Appearance.InfoBoxColors);

  LoadFormProperty(*wf, _T("prpAppAveNeedle"),
                   XCSoarInterface::main_window.vario->ShowAveNeedle);
  LoadFormProperty(*wf, _T("prpAppGaugeVarioSpeedToFly"),
                   XCSoarInterface::main_window.vario->ShowSpeedToFly);
  LoadFormProperty(*wf, _T("prpAppGaugeVarioAvgText"),
                   XCSoarInterface::main_window.vario->ShowAvgText);
  LoadFormProperty(*wf, _T("prpAppGaugeVarioGross"),
                   XCSoarInterface::main_window.vario->ShowGross);
  LoadFormProperty(*wf, _T("prpAppGaugeVarioMc"),
                   XCSoarInterface::main_window.vario->ShowMc);
  LoadFormProperty(*wf, _T("prpAppGaugeVarioBugs"),
                   XCSoarInterface::main_window.vario->ShowBugs);
  LoadFormProperty(*wf, _T("prpAppGaugeVarioBallast"),
                   XCSoarInterface::main_window.vario->ShowBallast);

  wp = (WndProperty*)wf->FindByName(_T("prpAutoBlank"));
  if (wp) {
    if (is_altair() || !is_embedded())
      wp->hide();
    DataFieldBoolean *df = (DataFieldBoolean *)wp->GetDataField();
    df->Set(XCSoarInterface::SettingsMap().EnableAutoBlank);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpFinishMinHeight"), ugAltitude,
                   settings_computer.ordered_defaults.finish_min_height);
  LoadFormProperty(*wf, _T("prpStartMaxHeight"), ugAltitude,
                   settings_computer.ordered_defaults.start_max_height);
  LoadFormProperty(*wf, _T("prpStartMaxHeightMargin"), ugAltitude,
                   settings_computer.start_max_height_margin);

  wp = (WndProperty*)wf->FindByName(_T("prpStartHeightRef"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("AGL"));
    dfe->addEnumText(_("MSL"));
    dfe->Set(settings_computer.ordered_defaults.start_max_height_ref);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpStartMaxSpeed"), ugHorizontalSpeed,
                   settings_computer.ordered_defaults.start_max_speed);
  LoadFormProperty(*wf, _T("prpStartMaxSpeedMargin"), ugHorizontalSpeed,
                   settings_computer.start_max_speed_margin);


  OrderedTask* temptask = protected_task_manager->task_blank();
  temptask->set_factory(TaskBehaviour::FACTORY_RT);

  wp = (WndProperty*)wf->FindByName(_T("prpStartType"));
  if (wp) {
    const AbstractTaskFactory::LegalPointVector point_types =
        temptask->get_factory().getValidStartTypes();
    DataFieldEnum* dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);

    for (unsigned i = 0; i < point_types.size(); i++) {
      dfe->addEnumText(OrderedTaskPointName(point_types[i]), (unsigned)point_types[i],
          OrderedTaskPointDescription(point_types[i]));
      if (point_types[i] == settings_computer.sector_defaults.start_type)
        dfe->Set((unsigned)point_types[i]);
    }
    wp->RefreshDisplay();
  }
  wp = (WndProperty*)wf->FindByName(_T("prpTurnpointType"));
  if (wp) {
    const AbstractTaskFactory::LegalPointVector point_types =
        temptask->get_factory().getValidIntermediateTypes();
    DataFieldEnum* dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);

    for (unsigned i = 0; i < point_types.size(); i++) {
      dfe->addEnumText(OrderedTaskPointName(point_types[i]),
          (unsigned)point_types[i],
          OrderedTaskPointDescription(point_types[i]));
      if (point_types[i] == settings_computer.sector_defaults.turnpoint_type) {
        dfe->Set((unsigned)point_types[i]);
      }
    }
    wp->RefreshDisplay();
  }
  wp = (WndProperty*)wf->FindByName(_T("prpFinishType"));
  if (wp) {
    const AbstractTaskFactory::LegalPointVector point_types =
        temptask->get_factory().getValidFinishTypes();
    DataFieldEnum* dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);

    for (unsigned i = 0; i < point_types.size(); i++) {
      dfe->addEnumText(OrderedTaskPointName(point_types[i]), (unsigned)point_types[i],
          OrderedTaskPointDescription(point_types[i]));
      if (point_types[i] == settings_computer.sector_defaults.finish_type)
        dfe->Set((unsigned)point_types[i]);
    }
    wp->RefreshDisplay();
  }
  wp = (WndProperty*)wf->FindByName(_T("prpTaskType"));
  if (wp) {
    const std::vector<TaskBehaviour::Factory_t> factory_types =
        temptask->get_factory_types();
    DataFieldEnum* dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->EnableItemHelp(true);

    for (unsigned i = 0; i < factory_types.size(); i++) {
      dfe->addEnumText(OrderedTaskFactoryName(factory_types[i]),
          (unsigned)factory_types[i], OrderedTaskFactoryDescription(
              factory_types[i]));
      if (factory_types[i] == settings_computer.task_type_default)
        dfe->Set((unsigned)factory_types[i]);
    }
    wp->RefreshDisplay();
  }

  delete temptask;

  LoadFormProperty(*wf, _T("prpStartRadius"),
                                  ugDistance,
                                  settings_computer.sector_defaults.start_radius);
  LoadFormProperty(*wf, _T("prpTurnpointRadius"),
                                  ugDistance,
                                  settings_computer.sector_defaults.turnpoint_radius);
  LoadFormProperty(*wf, _T("prpFinishRadius"),
                                  ugDistance,
                                  settings_computer.sector_defaults.finish_radius);
  LoadFormProperty(*wf, _T("prpAATMinTime"),
                   (unsigned)(settings_computer.ordered_defaults.aat_min_time / 60));


  LoadFormProperty(*wf, _T("prpLoggerTimeStepCruise"),
                   settings_computer.LoggerTimeStepCruise);
  LoadFormProperty(*wf, _T("prpLoggerTimeStepCircling"),
                   settings_computer.LoggerTimeStepCircling);
  LoadFormProperty(*wf, _T("prpSnailWidthScale"),
                   XCSoarInterface::SettingsMap().SnailScaling);

  wp = (WndProperty*)wf->FindByName(_T("prpSnailType"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    TCHAR tmp_text[30];
    _tcscpy(tmp_text, _("Vario"));
    _tcscat(tmp_text, _T(" #1"));
    dfe->addEnumText(tmp_text);
    _tcscpy(tmp_text, _("Vario"));
    _tcscat(tmp_text, _T(" #2"));
    dfe->addEnumText(tmp_text);
    dfe->addEnumText(_("Altitude"));
    dfe->Set((int)XCSoarInterface::SettingsMap().SnailType);
    wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(_T("prpRoutePlannerMode"));
  if (wp) {
    DataFieldEnum* dfe;
    dfe = (DataFieldEnum*)wp->GetDataField();
    dfe->addEnumText(_("None"));
    dfe->addEnumText(_("Terrain"));
    dfe->addEnumText(_("Airspace"));
    dfe->addEnumText(_("Both"));
    dfe->Set(settings_computer.route_planner.mode);
    wp->RefreshDisplay();
  }

  LoadFormProperty(*wf, _T("prpRoutePlannerAllowClimb"),
                   settings_computer.route_planner.allow_climb);

  LoadFormProperty(*wf, _T("prpRoutePlannerUseCeiling"),
                   settings_computer.route_planner.use_ceiling);

}

static bool
ProfileStringModified(const TCHAR *key, const TCHAR *new_value)
{
  TCHAR old_value[MAX_PATH];
  Profile::Get(key, old_value, MAX_PATH);
  return _tcscmp(old_value, new_value) != 0;
}

static bool
FinishFileField(const WndProperty &wp, const TCHAR *profile_key)
{
  const DataFieldFileReader *dfe =
    (const DataFieldFileReader *)wp.GetDataField();
  TCHAR new_value[MAX_PATH];
  _tcscpy(new_value, dfe->GetPathFile());
  ContractLocalPath(new_value);

  if (!ProfileStringModified(profile_key, new_value))
    return false;

  Profile::Set(profile_key, new_value);
  changed = true;
  return true;
}

static bool
FinishFileField(WndForm &wf, const TCHAR *control_name,
                const TCHAR *profile_key)
{
  const WndProperty *wp = (const WndProperty *)wf.FindByName(control_name);
  return wp != NULL && FinishFileField(*wp, profile_key);
}

/**
 * @return true if the value has changed
 */
static bool
FinishPortField(DeviceConfig &config, WndProperty &port_field)
{
  const DataFieldEnum &df = *(const DataFieldEnum *)port_field.GetDataField();
  int value = df.GetAsInteger();

  if (value < (int)num_port_types) {
    if (port_types[value].type == config.port_type)
      return false;

    config.port_type = port_types[value].type;
    return true;
  } else {
    value -= num_port_types;

#ifdef ANDROID
    if (config.port_type == DeviceConfig::RFCOMM &&
        _tcscmp(config.bluetooth_mac, df.GetAsString()) == 0)
      return false;
#else
    if (config.port_type == DeviceConfig::SERIAL &&
        value == (int)config.port_index)
      return false;
#endif

#ifdef ANDROID
    config.port_type = DeviceConfig::RFCOMM;
    config.bluetooth_mac = df.GetAsString();
#else
    config.port_type = DeviceConfig::SERIAL;
    config.port_index = value;
#endif
    return true;
  }
}

static bool
FinishDeviceFields(DeviceConfig &config,
                   WndProperty *port_field, WndProperty *speed_field,
                   WndProperty *driver_field)
{
  bool changed = false;

  if (port_field != NULL && FinishPortField(config, *port_field))
    changed = true;

#ifndef ANDROID
  if (speed_field != NULL &&
      (int)config.speed_index != speed_field->GetDataField()->GetAsInteger()) {
    config.speed_index = speed_field->GetDataField()->GetAsInteger();
    changed = true;
  }
#endif

  if (driver_field != NULL &&
      _tcscmp(config.driver_name,
              driver_field->GetDataField()->GetAsString()) != 0) {
    _tcscpy(config.driver_name, driver_field->GetDataField()->GetAsString());
    changed = true;
  }

  return changed;
}

static void
PrepareConfigurationDialog()
{
  gcc_unused ScopeBusyIndicator busy;

  loading = true;

  WndProperty *wp;

  wf = LoadDialog(CallBackTable, XCSoarInterface::main_window,
                  Layout::landscape ? _T("IDR_XML_CONFIGURATION_L") :
                                      _T("IDR_XML_CONFIGURATION"));
  if (wf == NULL)
    return;

  wf->SetKeyDownNotify(FormKeyDown);

  bool expert_mode = false;
  Profile::Get(szProfileUserLevel, expert_mode);

  CheckBox *cb = (CheckBox *)wf->FindByName(_T("Expert"));
  cb->set_checked(expert_mode);
  wf->FilterAdvanced(expert_mode);

  ((WndButton *)wf->FindByName(_T("cmdClose")))->SetOnClickNotify(OnCloseClicked);

  ((WndButton *)wf->FindByName(_T("cmdInfoBoxesCircling")))->
      SetOnClickNotify(OnInfoBoxesCircling);
  ((WndButton *)wf->FindByName(_T("cmdInfoBoxesCruise")))->
      SetOnClickNotify(OnInfoBoxesCruise);
  ((WndButton *)wf->FindByName(_T("cmdInfoBoxesFinalGlide")))->
      SetOnClickNotify(OnInfoBoxesFinalGlide);
  ((WndButton *)wf->FindByName(_T("cmdInfoBoxesAuxiliary")))->
      SetOnClickNotify(OnInfoBoxesAuxiliary);

  configuration_tabbed = ((TabbedControl *)wf->FindByName(_T("tabbed")));
  assert(configuration_tabbed != NULL);

  if (!is_windows_ce() || is_altair()) {
    wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxModel"));
    if (wp) {
      wp->hide();
    }
  }

  setVariables();

  /* restore previous page */
  configuration_tabbed->SetCurrentPage((unsigned)current_page);
  PageSwitched();

  changed = false;
  taskchanged = false;
  requirerestart = false;
  waypointneedsave = false;
  loading = false;
}

void dlgConfigurationShowModal(void)
{
  PrepareConfigurationDialog();

  if (wf->ShowModal() != mrOK) {
    delete wf;
    return;
  }

  /* save page number for next time this dialog is opened */
  current_page = (config_page)configuration_tabbed->GetCurrentPage();

  // TODO enhancement: implement a cancel button that skips all this
  // below after exit.

  SETTINGS_COMPUTER &settings_computer =
    XCSoarInterface::SetSettingsComputer();

  changed |= SaveFormProperty(*wf, _T("prpAbortSafetyUseCurrent"),
                              szProfileAbortSafetyUseCurrent,
                              settings_computer.safety_mc_use_current);

  /* GUI label is "Enable Auto Logger" */
  changed |= SaveFormPropertyNegated(*wf, _T("prpDisableAutoLogger"),
                                     szProfileDisableAutoLogger,
                                     settings_computer.DisableAutoLogger);

  WndProperty *wp;

  wp = (WndProperty*)wf->FindByName(_T("prpAbortTaskMode"));
  if (wp) {
    DataFieldEnum &df = *(DataFieldEnum *)wp->GetDataField();
    if ((int)settings_computer.abort_task_mode != df.GetAsInteger()) {
      settings_computer.abort_task_mode = (AbortTaskMode)df.GetAsInteger();
      Profile::Set(szProfileAbortTaskMode,
                   (unsigned)settings_computer.abort_task_mode);
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpSafetyMacCready"));
  if (wp) {
    DataFieldFloat &df = *(DataFieldFloat *)wp->GetDataField();
    fixed val = Units::ToSysVSpeed(df.GetAsFixed());
    if (settings_computer.safety_mc != val) {
      settings_computer.safety_mc = val;
      Profile::Set(szProfileSafetyMacCready,
                    iround(settings_computer.safety_mc*10));
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpRiskGamma"));
  if (wp) {
    DataFieldFloat &df = *(DataFieldFloat *)wp->GetDataField();
    fixed val = df.GetAsFixed();
    if (settings_computer.risk_gamma != val) {
      settings_computer.risk_gamma = val;
      Profile::Set(szProfileRiskGamma,
                    iround(settings_computer.risk_gamma*10));
      changed = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpSetSystemTimeFromGPS"),
                              szProfileSetSystemTimeFromGPS,
                              XCSoarInterface::SetSettingsMap().SetSystemTimeFromGPS);

  changed |= SaveFormProperty(*wf, _T("prpIgnoreNMEAChecksum"),
                              szProfileIgnoreNMEAChecksum,
                              NMEAParser::ignore_checksum);

  changed |= SaveFormProperty(*wf, _T("prpTrailDrift"),
                              szProfileTrailDrift,
                              XCSoarInterface::SetSettingsMap().EnableTrailDrift);
  changed |= SaveFormProperty(*wf, _T("prpDetourCostMarker"),
                              szProfileDetourCostMarker,
                              XCSoarInterface::SetSettingsMap().EnableDetourCostMarker);
  changed |= SaveFormPropertyEnum(*wf, _T("prpDisplayTrackBearing"),
                              szProfileDisplayTrackBearing,
                              XCSoarInterface::SetSettingsMap().DisplayTrackBearing);
  changed |= SaveFormProperty(*wf, _T("prpTrail"),
                              szProfileSnailTrail,
                              XCSoarInterface::SetSettingsMap().TrailActive);

  SimplePolar polar;
  PolarGlue::LoadFromProfile(polar);
  if (SaveFormToPolar(polar)) {
    PolarGlue::SaveToProfile(polar);
    changed  = PolarFileChanged = true;
  }

  short tmp = settings_computer.AltitudeMode;
  changed |= SaveFormProperty(*wf, _T("prpAirspaceDisplay"),
                              szProfileAltMode, tmp);
  settings_computer.AltitudeMode = (AirspaceDisplayMode_t)tmp;

  changed |= SaveFormProperty(*wf, _T("prpLoggerShortName"),
                              szProfileLoggerShort,
                              settings_computer.LoggerShortName);

  changed |= SaveFormProperty(*wf, _T("prpEnableFLARMMap"),
                              szProfileEnableFLARMMap,
                              XCSoarInterface::SetSettingsMap().EnableFLARMMap);

  changed |= SaveFormProperty(*wf, _T("prpEnableFLARMGauge"),
                              szProfileEnableFLARMGauge,
                              XCSoarInterface::SetSettingsMap().EnableFLARMGauge);

  changed |= SaveFormProperty(*wf, _T("prpAutoCloseFlarmDialog"),
                              szProfileAutoCloseFlarmDialog,
                              XCSoarInterface::SetSettingsMap().AutoCloseFlarmDialog);

  changed |= SaveFormProperty(*wf, _T("prpEnableTAGauge"),
                              szProfileEnableTAGauge,
                              XCSoarInterface::SetSettingsMap().EnableTAGauge);

  changed |= SaveFormProperty(*wf, _T("prpDebounceTimeout"),
                              szProfileDebounceTimeout,
                              XCSoarInterface::debounceTimeout);

  changed |= SaveFormProperty(*wf, _T("prpAirspaceOutline"),
                              szProfileAirspaceBlackOutline,
                              XCSoarInterface::SetSettingsMap().bAirspaceBlackOutline);

#ifndef ENABLE_OPENGL
  SETTINGS_MAP &settings_map = XCSoarInterface::SetSettingsMap();

  tmp = settings_map.AirspaceFillMode;
  changed |= SaveFormProperty(*wf, _T("prpAirspaceFillMode"),
                              szProfileAirspaceFillMode, tmp);
  settings_map.AirspaceFillMode = (enum SETTINGS_MAP::AirspaceFillMode)tmp;
#endif

  wp = (WndProperty*)wf->FindByName(_T("prpUTCOffset"));
  if (wp) {
    DataFieldFloat &df = *(DataFieldFloat *)wp->GetDataField();
    int ival = iround(df.GetAsFixed() * 3600);
    if (settings_computer.UTCOffset != ival) {
      settings_computer.UTCOffset = ival;

      // have to do this because registry variables can't be negative!
      if (ival < 0)
        ival += 24 * 3600;

      Profile::Set(szProfileUTCOffset, ival);
      changed = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpClipAltitude"), ugAltitude,
                              settings_computer.ClipAltitude,
                              szProfileClipAlt);

  changed |= SaveFormProperty(*wf, _T("prpAltWarningMargin"),
                              ugAltitude, settings_computer.airspace_warnings.AltWarningMargin,
                              szProfileAltMargin);

  changed |= SaveFormProperty(*wf, _T("prpAirspaceWarnings"),
                              szProfileAirspaceWarning,
                              settings_computer.EnableAirspaceWarnings);

  changed |= SaveFormProperty(*wf, _T("prpWarningTime"),
                              szProfileWarningTime,
                              settings_computer.airspace_warnings.WarningTime);

  changed |= SaveFormProperty(*wf, _T("prpAcknowledgementTime"),
                              szProfileAcknowledgementTime,
                              settings_computer.airspace_warnings.AcknowledgementTime);

  changed |= SaveFormPropertyEnum(*wf, _T("prpWaypointLabels"),
                                  szProfileDisplayText,
                                  XCSoarInterface::SetSettingsMap().DisplayTextType);

  changed |= SaveFormPropertyEnum(*wf, _T("prpWaypointLabelStyle"),
                                  szProfileWaypointLabelStyle,
                                  XCSoarInterface::SetSettingsMap().LandableRenderMode);

  changed |= SaveFormPropertyEnum(*wf, _T("prpWayPointLabelSelection"),
                                  szProfileWayPointLabelSelection,
                                  XCSoarInterface::SetSettingsMap().WayPointLabelSelection);

  changed |= SaveFormProperty(*wf, _T("prpEnableTerrain"),
                              szProfileDrawTerrain,
                              XCSoarInterface::SetSettingsMap().EnableTerrain);

  changed |= SaveFormProperty(*wf, _T("prpEnableTopography"),
                              szProfileDrawTopography,
                              XCSoarInterface::SetSettingsMap().EnableTopography);

  wp = (WndProperty*)wf->FindByName(_T("prpSlopeShadingType"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().SlopeShadingType !=
        wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().SlopeShadingType =
          (SlopeShadingType_t)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileSlopeShadingType,
                   XCSoarInterface::SettingsMap().SlopeShadingType);
      changed = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpCirclingZoom"),
                              szProfileCircleZoom,
                              XCSoarInterface::SetSettingsMap().CircleZoom);

  changed |= SaveFormProperty(*wf, _T("prpMaxAutoZoomDistance"),
                              ugDistance,
                              XCSoarInterface::SetSettingsMap().MaxAutoZoomDistance,
                              szProfileMaxAutoZoomDistance);

  wp = (WndProperty*)wf->FindByName(_T("prpOrientationCruise"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().OrientationCruise != wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().OrientationCruise = (DisplayOrientation_t)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileOrientationCruise,
                    XCSoarInterface::SettingsMap().OrientationCruise);
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpOrientationCircling"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().OrientationCircling != wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().OrientationCircling = (DisplayOrientation_t)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileOrientationCircling,
                    XCSoarInterface::SettingsMap().OrientationCircling);
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpMapShiftBias"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().MapShiftBias != wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().MapShiftBias = (MapShiftBias_t)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileMapShiftBias,
                   XCSoarInterface::SettingsMap().MapShiftBias);
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpMenuTimeout"));
  if (wp) {
    if ((int)XCSoarInterface::MenuTimeoutMax != wp->GetDataField()->GetAsInteger()*2) {
      XCSoarInterface::MenuTimeoutMax = wp->GetDataField()->GetAsInteger()*2;
      Profile::Set(szProfileMenuTimeout,XCSoarInterface::MenuTimeoutMax);
      changed = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpSafetyAltitudeArrival"), ugAltitude,
                              settings_computer.safety_height_arrival,
                              szProfileSafetyAltitudeArrival);

  changed |= SaveFormProperty(*wf, _T("prpSafetyAltitudeTerrain"), ugAltitude,
                              settings_computer.route_planner.safety_height_terrain,
                              szProfileSafetyAltitudeTerrain);

  changed |= SaveFormProperty(*wf, _T("prpAutoWind"), szProfileAutoWind,
                              settings_computer.AutoWindMode);

  changed |= SaveFormProperty(*wf, _T("prpExternalWind"),
                              szProfileExternalWind,
                              settings_computer.ExternalWind);

  changed |= SaveFormProperty(*wf, _T("prpWindArrowStyle"),
                              szProfileWindArrowStyle,
                              XCSoarInterface::SetSettingsMap().WindArrowStyle);

  int auto_mc_mode = (int)settings_computer.auto_mc_mode;
  changed |= SaveFormProperty(*wf, _T("prpAutoMcMode"), szProfileAutoMcMode,
                              auto_mc_mode);
  settings_computer.auto_mc_mode = (TaskBehaviour::AutoMCMode_t)auto_mc_mode;

  changed |= SaveFormProperty(*wf, _T("prpEnableNavBaroAltitude"),
                              szProfileEnableNavBaroAltitude,
                              settings_computer.EnableNavBaroAltitude);

  changed |= SaveFormProperty(*wf, _T("prpFinalGlideTerrain"),
                              szProfileFinalGlideTerrain,
                              settings_computer.FinalGlideTerrain);

  changed |= SaveFormProperty(*wf, _T("prpBlockSTF"),
                              szProfileBlockSTF,
                              settings_computer.EnableBlockSTF);

  {
    unsigned t= settings_computer.contest;
    changed |= SaveFormProperty(*wf, _T("prpContests"), szProfileOLCRules,
                                t);
    settings_computer.contest = (Contests)t;
  }

  changed |= SaveFormProperty(*wf, _T("prpHandicap"), szProfileHandicap,
                              settings_computer.contest_handicap);

  WaypointFileChanged = WaypointFileChanged |
    FinishFileField(*wf, _T("prpWaypointFile"), szProfileWayPointFile) |
    FinishFileField(*wf, _T("prpAdditionalWaypointFile"),
                    szProfileAdditionalWayPointFile);

  AirspaceFileChanged =
    FinishFileField(*wf, _T("prpAirspaceFile"), szProfileAirspaceFile) |
    FinishFileField(*wf, _T("prpAdditionalAirspaceFile"),
                    szProfileAdditionalAirspaceFile);

  MapFileChanged = FinishFileField(*wf, _T("prpMapFile"), szProfileMapFile);

  TerrainFileChanged = FinishFileField(*wf, _T("prpTerrainFile"),
                                       szProfileTerrainFile);

  TopographyFileChanged = FinishFileField(*wf, _T("prpTopographyFile"),
                                        szProfileTopographyFile);

  AirfieldFileChanged = FinishFileField(*wf, _T("prpAirfieldFile"),
                                        szProfileAirfieldFile);

  wp = (WndProperty *)wf->FindByName(_T("prpLanguageFile"));
  if (wp != NULL) {
    DataFieldEnum &df = *(DataFieldEnum *)wp->GetDataField();

    TCHAR old_value[MAX_PATH];
    if (!Profile::GetPath(szProfileLanguageFile, old_value))
      old_value[0] = _T('\0');

    const TCHAR *old_base = BaseName(old_value);
    if (old_base == NULL)
      old_base = old_value;

    TCHAR buffer[MAX_PATH];
    const TCHAR *new_value, *new_base;

    switch (df.GetAsInteger()) {
    case 0:
      new_value = new_base = _T("auto");
      break;

    case 1:
      new_value = new_base = _T("none");
      break;

    default:
      _tcscpy(buffer, df.GetAsString());
      ContractLocalPath(buffer);
      new_value = buffer;
      new_base = BaseName(new_value);
      if (new_base == NULL)
        new_base = new_value;
      break;
    }

    if (_tcscmp(old_value, new_value) != 0 &&
        _tcscmp(old_base, new_base) != 0) {
      Profile::Set(szProfileLanguageFile, new_value);
      LanguageChanged = changed = true;
    }
  }

  if (FinishFileField(*wf, _T("prpStatusFile"), szProfileStatusFile))
    requirerestart = true;

  if (FinishFileField(*wf, _T("prpInputFile"), szProfileInputFile))
    requirerestart = true;

  changed |= SaveFormProperty(*wf, _T("prpBallastSecsToEmpty"),
                              szProfileBallastSecsToEmpty,
                              settings_computer.BallastSecsToEmpty);

  changed |= SaveFormProperty(*wf, _T("prpMaxManoeuveringSpeed"),
                              ugHorizontalSpeed, settings_computer.SafetySpeed,
                              szProfileSafteySpeed);

  wp = (WndProperty*)wf->FindByName(_T("prpAppIndFinalGlide"));
  if (wp) {
    if (Appearance.IndFinalGlide != (IndFinalGlide_t)(wp->GetDataField()->GetAsInteger())) {
      Appearance.IndFinalGlide = (IndFinalGlide_t)(wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileAppIndFinalGlide, Appearance.IndFinalGlide);
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxBorder"));
  if (wp) {
    if (Appearance.InfoBoxBorder != (InfoBoxBorderAppearance_t)
        (wp->GetDataField()->GetAsInteger())) {
      Appearance.InfoBoxBorder = (InfoBoxBorderAppearance_t)
        (wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileAppInfoBoxBorder,
                    Appearance.InfoBoxBorder);
      changed = true;
      requirerestart = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpGestures"), szProfileGestures,
                              settings_computer.EnableGestures);

  wp = (WndProperty*)wf->FindByName(_T("prpAverEffTime")); // VENTA6
  if (wp) {
    if (settings_computer.AverEffTime != wp->GetDataField()->GetAsInteger()) {
      settings_computer.AverEffTime = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileAverEffTime,
                   settings_computer.AverEffTime);
      changed = true;
      requirerestart = true;
    }
  }

  bool info_box_geometry_changed = false;
  wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxGeom"));
  if (wp) {
    if (InfoBoxLayout::InfoBoxGeometry !=
        (InfoBoxLayout::Geometry)wp->GetDataField()->GetAsInteger()) {
      InfoBoxLayout::InfoBoxGeometry =
          (InfoBoxLayout::Geometry)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileInfoBoxGeometry,
                   (unsigned)InfoBoxLayout::InfoBoxGeometry);
      changed = true;
      info_box_geometry_changed = true;
    }
  }

  if (Display::RotateSupported()) {
    wp = (WndProperty*)wf->FindByName(_T("prpDisplayOrientation"));
    assert(wp != NULL);

    const DataFieldEnum *dfe = (const DataFieldEnum *)wp->GetDataField();
    Display::orientation orientation =
      (Display::orientation)dfe->GetAsInteger();
    if (orientation != Profile::GetDisplayOrientation()) {
      Profile::SetDisplayOrientation(orientation);
      changed = true;
      requirerestart = true;
    }
  }

#if defined(_WIN32_WCE) && !defined(GNAV)
  // VENTA-ADDON MODEL CHANGE
  wp = (WndProperty*)wf->FindByName(_T("prpAppInfoBoxModel"));
  if (wp) {
    if (GlobalModelType != (ModelType)wp->GetDataField()->GetAsInteger()) {
      GlobalModelType = (ModelType)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileAppInfoBoxModel,
                    GlobalModelType);
      changed = true;
      requirerestart = true;
    }
  }
//
#endif

  wp = (WndProperty*)wf->FindByName(_T("prpAppStatusMessageAlignment"));
  if (wp) {
    if (Appearance.StateMessageAlign != (StateMessageAlign_t)
        (wp->GetDataField()->GetAsInteger())) {
      Appearance.StateMessageAlign = (StateMessageAlign_t)
        (wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileAppStatusMessageAlignment,
                    Appearance.StateMessageAlign);
      changed = true;
    }
  }

  if (has_pointer()) {
    wp = (WndProperty*)wf->FindByName(_T("prpTextInput"));
    assert(wp != NULL);
    if (Appearance.TextInputStyle != (TextInputStyle_t)(wp->GetDataField()->GetAsInteger())) {
      Appearance.TextInputStyle = (TextInputStyle_t)(wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileAppTextInputStyle, Appearance.TextInputStyle);
      changed = true;
    }
  }

    wp = (WndProperty*)wf->FindByName(_T("prpTabDialogStyle"));
    assert(wp != NULL);
    if (Appearance.DialogTabStyle != (DialogTabStyle_t)(wp->GetDataField()->GetAsInteger())) {
      Appearance.DialogTabStyle = (DialogTabStyle_t)(wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileAppDialogTabStyle, Appearance.DialogTabStyle);
      changed = true;
    }

  wp = (WndProperty*)wf->FindByName(_T("prpDialogStyle"));
  if (wp)
    {
      if (DialogStyleSetting != (DialogStyle)(wp->GetDataField()->GetAsInteger()))
        {
          DialogStyleSetting = (DialogStyle)(wp->GetDataField()->GetAsInteger());
          Profile::Set(szProfileAppDialogStyle, DialogStyleSetting);
          changed = true;
        }
    }

  wp = (WndProperty*)wf->FindByName(_T("prpAppIndLandable"));
  if (wp) {
    if (Appearance.IndLandable != (IndLandable_t)(wp->GetDataField()->GetAsInteger())) {
      Appearance.IndLandable = (IndLandable_t)(wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileAppIndLandable, Appearance.IndLandable);
      changed = true;
      Graphics::InitLandableIcons();
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpAppUseSWLandablesRendering"),
                              szProfileAppUseSWLandablesRendering,
                              Appearance.UseSWLandablesRendering);

  changed |= SaveFormProperty(*wf, _T("prpAppLandableRenderingScale"),
                              szProfileAppLandableRenderingScale,
                              Appearance.LandableRenderingScale);

  changed |= SaveFormProperty(*wf, _T("prpAppScaleRunwayLength"),
                              szProfileAppScaleRunwayLength,
                              Appearance.ScaleRunwayLength);

  changed |= SaveFormProperty(*wf, _T("prpEnableExternalTriggerCruise"),
                              szProfileEnableExternalTriggerCruise,
                              settings_computer.EnableExternalTriggerCruise);

  changed |= requirerestart |=
    SaveFormProperty(*wf, _T("prpAppInverseInfoBox"),
                     szProfileAppInverseInfoBox, Appearance.InverseInfoBox);

  changed |= SaveFormProperty(*wf, _T("prpGliderScreenPosition"),
                              szProfileGliderScreenPosition,
                              XCSoarInterface::SetSettingsMap().GliderScreenPosition);

  changed |= SaveFormProperty(*wf, _T("prpAppAveNeedle"), szProfileAppAveNeedle,
                              XCSoarInterface::main_window.vario->ShowAveNeedle);

  changed |= requirerestart |=
    SaveFormProperty(*wf, _T("prpAppInfoBoxColors"),
                     szProfileAppInfoBoxColors, Appearance.InfoBoxColors);

  changed |= SaveFormProperty(*wf, _T("prpAppGaugeVarioSpeedToFly"),
                              szProfileAppGaugeVarioSpeedToFly,
                              XCSoarInterface::main_window.vario->ShowSpeedToFly);

  changed |= SaveFormProperty(*wf, _T("prpAppGaugeVarioAvgText"),
                              szProfileAppGaugeVarioAvgText,
                              XCSoarInterface::main_window.vario->ShowAvgText);

  changed |= SaveFormProperty(*wf, _T("prpAppGaugeVarioGross"),
                              szProfileAppGaugeVarioGross,
                              XCSoarInterface::main_window.vario->ShowGross);

  changed |= SaveFormProperty(*wf, _T("prpAppGaugeVarioMc"),
                              szProfileAppGaugeVarioMc,
                              XCSoarInterface::main_window.vario->ShowMc);

  changed |= SaveFormProperty(*wf, _T("prpAppGaugeVarioBugs"),
                              szProfileAppGaugeVarioBugs,
                              XCSoarInterface::main_window.vario->ShowBugs);

  changed |= SaveFormProperty(*wf, _T("prpAppGaugeVarioBallast"),
                              szProfileAppGaugeVarioBallast,
                              XCSoarInterface::main_window.vario->ShowBallast);

#ifdef HAVE_BLANK
  changed |= SaveFormProperty(*wf, _T("prpAutoBlank"),
                              szProfileAutoBlank,
                              XCSoarInterface::SetSettingsMap().EnableAutoBlank);
#endif

  wp = (WndProperty*)wf->FindByName(_T("prpTerrainContrast"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().TerrainContrast * 100 / 255 !=
        wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().TerrainContrast = (short)(wp->GetDataField()->GetAsInteger() * 255 / 100);
      Profile::Set(szProfileTerrainContrast,XCSoarInterface::SettingsMap().TerrainContrast);
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpTerrainBrightness"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().TerrainBrightness * 100 / 255 !=
        wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().TerrainBrightness = (short)(wp->GetDataField()->GetAsInteger() * 255 / 100);
      Profile::Set(szProfileTerrainBrightness,
                    XCSoarInterface::SettingsMap().TerrainBrightness);
      changed = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpTerrainRamp"), szProfileTerrainRamp,
                              XCSoarInterface::SetSettingsMap().TerrainRamp);

  taskchanged |= SaveFormProperty(*wf, _T("prpFinishMinHeight"), ugAltitude,
                                  settings_computer.ordered_defaults.finish_min_height,
                                  szProfileFinishMinHeight);

  taskchanged |= SaveFormProperty(*wf, _T("prpStartMaxHeight"), ugAltitude,
                                  settings_computer.ordered_defaults.start_max_height,
                                  szProfileStartMaxHeight);

  taskchanged |= SaveFormProperty(*wf, _T("prpStartMaxHeightMargin"), ugAltitude,
                                  settings_computer.start_max_height_margin,
                                  szProfileStartMaxHeightMargin);

  taskchanged |= SaveFormProperty(*wf, _T("prpStartHeightRef"), ugAltitude,
                                  settings_computer.ordered_defaults.start_max_height_ref,
                                  szProfileStartHeightRef);

  taskchanged |= SaveFormProperty(*wf, _T("prpStartMaxSpeed"),
                                  ugHorizontalSpeed,
                                  settings_computer.ordered_defaults.start_max_speed,
                                  szProfileStartMaxSpeed);

  taskchanged |= SaveFormProperty(*wf, _T("prpStartMaxSpeedMargin"),
                                  ugHorizontalSpeed,
                                  settings_computer.start_max_speed_margin,
                                  szProfileStartMaxSpeedMargin);

  unsigned sdtemp = 0;
  sdtemp = (unsigned)settings_computer.sector_defaults.start_type;
  taskchanged |= SaveFormProperty(*wf, _T("prpStartType"),
                                  szProfileStartType,
                                  sdtemp);
  settings_computer.sector_defaults.start_type =
      (AbstractTaskFactory::LegalPointType_t)sdtemp;

  taskchanged |= SaveFormProperty(*wf, _T("prpStartRadius"),
                                  ugDistance,
                                  settings_computer.sector_defaults.start_radius,
                                  szProfileStartRadius);

  sdtemp = (unsigned)settings_computer.sector_defaults.turnpoint_type;
  taskchanged |= SaveFormProperty(*wf, _T("prpTurnpointType"),
                                  szProfileTurnpointType,
                                  sdtemp);
  settings_computer.sector_defaults.turnpoint_type =
      (AbstractTaskFactory::LegalPointType_t)sdtemp;

  taskchanged |= SaveFormProperty(*wf, _T("prpTurnpointRadius"),
                                  ugDistance,
                                  settings_computer.sector_defaults.turnpoint_radius,
                                  szProfileTurnpointRadius);

  sdtemp = (unsigned)settings_computer.sector_defaults.finish_type;
  taskchanged |= SaveFormProperty(*wf, _T("prpFinishType"),
                                  szProfileFinishType,
                                  sdtemp);
  settings_computer.sector_defaults.finish_type =
      (AbstractTaskFactory::LegalPointType_t)sdtemp;

  taskchanged |= SaveFormProperty(*wf, _T("prpFinishRadius"),
                                  ugDistance,
                                  settings_computer.sector_defaults.finish_radius,
                                  szProfileFinishRadius);

  sdtemp = (unsigned)settings_computer.task_type_default;
  taskchanged |= SaveFormProperty(*wf, _T("prpTaskType"),
                                  szProfileTaskType,
                                  sdtemp);
  settings_computer.task_type_default =
      (TaskBehaviour::Factory_t)sdtemp;


  unsigned aatminutes = unsigned(settings_computer.ordered_defaults.aat_min_time) / 60;
  wp = (WndProperty*)wf->FindByName(_T("prpAATMinTime"));
  if (aatminutes != (unsigned)wp->GetDataField()->GetAsInteger()) {
    aatminutes = wp->GetDataField()->GetAsInteger();
    settings_computer.ordered_defaults.aat_min_time = fixed(aatminutes * 60);
    Profile::Set(szProfileAATMinTime, aatminutes * 60);
    taskchanged = true;
  }

  changed |= taskchanged;

  changed |= SaveFormProperty(*wf, _T("prpLoggerTimeStepCruise"),
                              szProfileLoggerTimeStepCruise,
                              settings_computer.LoggerTimeStepCruise);

  changed |= SaveFormProperty(*wf, _T("prpLoggerTimeStepCircling"),
                              szProfileLoggerTimeStepCircling,
                              settings_computer.LoggerTimeStepCircling);

  DevicePortChanged =
    FinishDeviceFields(device_config[0],
                       (WndProperty*)wf->FindByName(_T("prpComPort1")),
                       (WndProperty*)wf->FindByName(_T("prpComSpeed1")),
                       (WndProperty*)wf->FindByName(_T("prpComDevice1")));

  DevicePortChanged =
    FinishDeviceFields(device_config[1],
                       (WndProperty*)wf->FindByName(_T("prpComPort2")),
                       (WndProperty*)wf->FindByName(_T("prpComSpeed2")),
                       (WndProperty*)wf->FindByName(_T("prpComDevice2"))) ||
    DevicePortChanged;

  if (DevicePortChanged)
    changed = true;

  bool snailscaling_changed =
      SaveFormProperty(*wf, _T("prpSnailWidthScale"),
                       szProfileSnailWidthScale,
                       XCSoarInterface::SetSettingsMap().SnailScaling);
  changed |= snailscaling_changed;
  if (snailscaling_changed)
    Graphics::InitSnailTrail(XCSoarInterface::SettingsMap());

  wp = (WndProperty*)wf->FindByName(_T("prpSnailType"));
  if (wp) {
    if (XCSoarInterface::SettingsMap().SnailType != (SnailType_t)wp->GetDataField()->GetAsInteger()) {
      XCSoarInterface::SetSettingsMap().SnailType = (SnailType_t)wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileSnailType, (int)XCSoarInterface::SettingsMap().SnailType);
      changed = true;
      Graphics::InitSnailTrail(XCSoarInterface::SettingsMap());
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpRoutePlannerMode"));
  if (wp) {
    DataFieldEnum &df = *(DataFieldEnum *)wp->GetDataField();
    if ((int)settings_computer.route_planner.mode != df.GetAsInteger()) {
      settings_computer.route_planner.mode = (RoutePlannerConfig::Mode)df.GetAsInteger();
      Profile::Set(szProfileRoutePlannerMode,
                   (unsigned)settings_computer.route_planner.mode);
      changed = true;
    }
  }

  changed |= SaveFormProperty(*wf, _T("prpRoutePlannerAllowClimb"),
                              szProfileRoutePlannerAllowClimb,
                              settings_computer.route_planner.allow_climb);

  changed |= SaveFormProperty(*wf, _T("prpRoutePlannerUseCeiling"),
                              szProfileRoutePlannerUseCeiling,
                              settings_computer.route_planner.use_ceiling);

  /* the Units settings affect how other form values are read and translated
   * so changes to Units settings should be processed after all other form settings
   */
  wp = (WndProperty*)wf->FindByName(_T("prpUnitsSpeed"));
  if (wp) {
    if ((int)SpeedUnits != wp->GetDataField()->GetAsInteger()) {
      SpeedUnits = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileSpeedUnitsValue, SpeedUnits);
      changed = true;

      switch (SpeedUnits) {
      case 0:
        Units::SetUserSpeedUnit(unStatuteMilesPerHour);
        Units::SetUserWindSpeedUnit(unStatuteMilesPerHour);
        break;
      case 1:
        Units::SetUserSpeedUnit(unKnots);
        Units::SetUserWindSpeedUnit(unKnots);
        break;
      case 2:
      default:
        Units::SetUserSpeedUnit(unKiloMeterPerHour);
        Units::SetUserWindSpeedUnit(unKiloMeterPerHour);
        break;
      }
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsLatLon"));
  if (wp) {
    if ((int)Units::GetCoordinateFormat() != wp->GetDataField()->GetAsInteger()) {
      Units::SetCoordinateFormat(
          (CoordinateFormats_t)wp->GetDataField()->GetAsInteger());
      Profile::Set(szProfileLatLonUnits, Units::GetCoordinateFormat());
      changed = true;
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsTaskSpeed"));
  if (wp) {
    if ((int)TaskSpeedUnits != wp->GetDataField()->GetAsInteger()) {
      TaskSpeedUnits = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileTaskSpeedUnitsValue, TaskSpeedUnits);
      changed = true;

      switch (TaskSpeedUnits) {
      case 0:
        Units::SetUserTaskSpeedUnit(unStatuteMilesPerHour);
        break;
      case 1:
        Units::SetUserTaskSpeedUnit(unKnots);
        break;
      case 2:
      default:
        Units::SetUserTaskSpeedUnit(unKiloMeterPerHour);
        break;
      }
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsDistance"));
  if (wp) {
    if ((int)DistanceUnits != wp->GetDataField()->GetAsInteger()) {
      DistanceUnits = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileDistanceUnitsValue, DistanceUnits);
      changed = true;

      switch (DistanceUnits) {
      case 0:
        Units::SetUserDistanceUnit(unStatuteMiles);
        break;
      case 1:
        Units::SetUserDistanceUnit(unNauticalMiles);
        break;
      case 2:
      default:
        Units::SetUserDistanceUnit(unKiloMeter);
        break;
      }
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsLift"));
  if (wp) {
    if ((int)LiftUnits != wp->GetDataField()->GetAsInteger()) {
      LiftUnits = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileLiftUnitsValue, LiftUnits);
      changed = true;

      switch (LiftUnits) {
      case 0:
        Units::SetUserVerticalSpeedUnit(unKnots);
        break;
      case 1:
      default:
        Units::SetUserVerticalSpeedUnit(unMeterPerSecond);
        break;
      case 2:
        Units::SetUserVerticalSpeedUnit(unFeetPerMinute);
        break;
      }
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsAltitude"));
  if (wp) {
    if ((int)AltitudeUnits != wp->GetDataField()->GetAsInteger()) {
      AltitudeUnits = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileAltitudeUnitsValue, AltitudeUnits);
      changed = true;

      switch (AltitudeUnits) {
      case 0:
        Units::SetUserAltitudeUnit(unFeet);
        break;
      case 1:
      default:
        Units::SetUserAltitudeUnit(unMeter);
        break;
      }
    }
  }

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsTemperature"));
  if (wp) {
    if ((int)TemperatureUnits != wp->GetDataField()->GetAsInteger()) {
      TemperatureUnits = wp->GetDataField()->GetAsInteger();
      Profile::Set(szProfileTemperatureUnitsValue, TemperatureUnits);
      changed = true;

      switch (TemperatureUnits) {
      case 0:
        Units::SetUserTemperatureUnit(unGradCelcius);
        break;
      case 1:
      default:
        Units::SetUserTemperatureUnit(unGradFahrenheit);
        break;
      }
    }
  }

  if (PolarFileChanged && protected_task_manager != NULL) {
    GlidePolar gp = protected_task_manager->get_glide_polar();
    PolarGlue::LoadFromProfile(gp);
    protected_task_manager->set_glide_polar(gp);
  }

  changed |= PagesConfigPanel::Save(wf);

  if (DevicePortChanged)
    for (unsigned i = 0; i < NUMDEV; ++i)
      Profile::SetDeviceConfig(i, device_config[i]);

  if (!is_embedded() && DevicePortChanged)
    requirerestart = true;

  if (info_box_geometry_changed)
    XCSoarInterface::main_window.ReinitialiseLayout();

  if (changed) {
    Profile::Save();
    LogDebug(_T("Configuration: Changes saved"));
    if (requirerestart)
      MessageBoxX(_("Changes to configuration saved.  Restart XCSoar to apply changes."),
                  _T(""), MB_OK);
  }

  delete wf;
  wf = NULL;
}
