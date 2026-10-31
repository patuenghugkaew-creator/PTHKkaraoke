#ifndef THEME_H
#define THEME_H

class QApplication;

// Application color theme: follow Windows, always dark or always light.
namespace Theme {

enum Mode { System = 0, Dark = 1, Light = 2, DarkOrange = 3 };

void init(QApplication &app);   // load saved mode, apply it, watch Windows setting
Mode mode();                    // selected mode
void setMode(Mode m);           // apply immediately and save
bool isDark();                  // theme currently in effect (dark orange counts as dark)
bool isOrange();                // the dark orange theme is in effect

// size of the right-click menus (text and icons): 0 normal, 1 large, 2 extra large
int menuSize();
void setMenuSize(int size);     // apply immediately and save

}

#endif // THEME_H
