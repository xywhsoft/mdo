import { UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import nativeProbe from 'libmdoprobe.so';
import { hilog } from '@kit.PerformanceAnalysisKit';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want): void {
    // A separate launch can request TCC after the ordinary AOT/Web checks.
    AppStorage.setOrCreate('probeTcc', want.parameters?.['probeTcc'] === '1');
    const fileResult = nativeProbe.fileCheck(this.context.filesDir);
    const spawnResult = nativeProbe.spawnCheck();
    const childResult = nativeProbe.nativeChildCheck();
    AppStorage.setOrCreate('fileResult', fileResult);
    AppStorage.setOrCreate('spawnResult', spawnResult);
    AppStorage.setOrCreate('childResult', childResult);
    hilog.info(0, 'MdoProbe', 'file_result=%{public}d spawn_result=%{public}d', fileResult, spawnResult);
    hilog.info(0, 'MdoProbe', 'native_child_result=%{public}d', childResult);
  }
  onWindowStageCreate(stage: window.WindowStage): void {
    stage.loadContent('pages/Index', (error) => {
      if (error.code) hilog.error(0, 'MdoProbe', 'load_content_error=%{public}d', error.code);
    });
  }
}
