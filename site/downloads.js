// Points the download buttons at the files of the latest GitHub Release. Release files carry the version in their
// names (WhistleSim-v0.4.0-macOS.zip), so a fixed link can't name them. Without this script, or if GitHub's API
// can't be reached, the buttons keep their fallback link: the latest release's page.
(function () {
  var api = "https://api.github.com/repos/petrushy/whistlesim/releases/latest";
  var buttons = document.querySelectorAll("[data-asset-suffix]");
  if (!buttons.length || !window.fetch) return;

  fetch(api, { headers: { Accept: "application/vnd.github+json" } })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (release) {
      buttons.forEach(function (button) {
        var suffix = button.getAttribute("data-asset-suffix");
        var asset = (release.assets || []).find(function (a) { return a.name.endsWith(suffix); });
        if (asset) button.href = asset.browser_download_url;
      });
      var version = document.getElementById("latest-version");
      if (version && release.tag_name) {
        version.textContent = "Latest version: " + release.tag_name.replace(/^v/, "") + ".";
      }
    })
    .catch(function () { /* keep the fallback links */ });
})();
