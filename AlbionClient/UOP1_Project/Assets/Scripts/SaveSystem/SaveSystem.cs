using System.Collections;
using UnityEngine;

// MMO 모드: 서버가 권위 소스이므로 로컬 세이브는 비활성화.
// 기존 callers 호환을 위해 클래스 shell과 시그니처만 유지, 메서드 본문은 no-op.
public class SaveSystem : ScriptableObject
{
	[SerializeField] private VoidEventChannelSO _saveSettingsEvent = default;
	[SerializeField] private LoadEventChannelSO _loadLocation = default;
	[SerializeField] private InventorySO _playerInventory = default;
	[SerializeField] private SettingsSO _currentSettings = default;
	[SerializeField] private QuestManagerSO _questManagerSO = default;

	public string saveFilename = "save.chop";
	public string backupSaveFilename = "save.chop.bak";
	public Save saveData = new Save();

	public bool LoadSaveDataFromDisk() => false;
	public IEnumerator LoadSavedInventory() { yield break; }
	public void LoadSavedQuestlineStatus() { }
	public void SaveDataToDisk() { }
	public void WriteEmptySaveFile() { }
	public void SetNewGameData() { _playerInventory?.Init(); _questManagerSO?.ResetQuestlines(); }
}
