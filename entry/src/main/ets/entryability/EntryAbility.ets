import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';

import simulator from 'libentry.so';

const DOMAIN = 0x3200;
const TAG = 'ThreeBodySimulator';

export default class EntryAbility extends UIAbility {
  onBackground(): void { simulator.stop(); }

  onWindowStageDestroy(): void { simulator.stop(); simulator.setOutputWindow(0); }

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(DOMAIN, TAG, 'EntryAbility created');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    const mainWindow = windowStage.getMainWindowSync();
    simulator.setOutputWindow(mainWindow.getWindowProperties().id);
    mainWindow.setWindowLayoutFullScreen(true).catch(() => {
      hilog.warn(DOMAIN, TAG, 'Fullscreen layout unavailable');
    });
    mainWindow.setWindowSystemBarEnable([]).catch(() => {
      hilog.warn(DOMAIN, TAG, 'System bars could not be hidden');
    });
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(DOMAIN, TAG, 'Failed to load pages/Index: %{public}s', JSON.stringify(err));
        return;
      }
      hilog.info(DOMAIN, TAG, 'Main page loaded');
    });
  }
}
