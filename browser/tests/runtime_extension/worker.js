// Test fixture: an ordinary MV3 worker using upstream extension APIs only.
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (sender.id !== chrome.runtime.id || message.operation !== 'roundtrip') {
    return false;
  }
  chrome.storage.local.set({smokeValue: message.value}).then(
      () => chrome.storage.local.get('smokeValue')).then(
      stored => respond({value: stored.smokeValue}));
  return true;
});
