#ifndef INPUTEVENTS_H
#define INPUTEVENTS_H

#include "XCSoar.h"
#include "externs.h"

typedef void (*pt2Event)(TCHAR *);

class InputEvents {
 public:
  static void readFile();
  static int mode2int(TCHAR *mode, bool create);
  static void setMode(TCHAR *mode);
  static TCHAR* getMode();
  static int getModeID();
  static int InputEvents::findKey(TCHAR *data);
  static pt2Event InputEvents::findEvent(TCHAR *);
  static bool processKey(int key);
  static bool processNmea(TCHAR *data);
  static bool processButton(int bindex);
  static void processGo(int event_id);
  static int makeEvent(void (*event)(TCHAR *), TCHAR *misc);
  static void makeLabel(int mode_id, TCHAR *label, int location, int event_id);
  static void eventScreenModes(TCHAR *misc);
  static void eventSnailTrail(TCHAR *misc);
  static void eventSounds(TCHAR *misc);
  static void eventMarkLocation(TCHAR *misc);
  static void eventAutoZoom(TCHAR *misc);
  static void eventScaleZoom(TCHAR *misc);
  static void eventPan(TCHAR *misc);
  static void eventClearWarningsAndTerrain(TCHAR *misc);
  static void eventSelectInfoBox(TCHAR *misc);
  static void eventChangeInfoBoxType(TCHAR *misc);
  static void eventDoInfoKey(TCHAR *misc);
  static void eventPanCursor(TCHAR *misc);
  static void eventMainMenu(TCHAR *misc);
  static void eventMode(TCHAR *misc);

  static void eventStatus(TCHAR *misc);
  static void eventAnalysis(TCHAR *misc);
  static void eventWaypointDetails(TCHAR *misc);

};


#endif
