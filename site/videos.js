// Swaps a video's thumbnail for the YouTube player when it is clicked, so the page contacts YouTube only then.
// Without this script the thumbnails stay plain links to the videos on YouTube.
(function () {
  document.querySelectorAll("a.video[data-video-id]").forEach(function (link) {
    link.addEventListener("click", function (event) {
      event.preventDefault();
      var frame = document.createElement("iframe");
      frame.src = "https://www.youtube-nocookie.com/embed/" + link.getAttribute("data-video-id") + "?autoplay=1";
      frame.title = link.getAttribute("data-video-title") || "Video";
      frame.allow = "autoplay; encrypted-media; picture-in-picture; fullscreen";
      frame.allowFullscreen = true;
      frame.referrerPolicy = "strict-origin-when-cross-origin";
      frame.className = "video";
      link.replaceWith(frame);
    });
  });
})();
