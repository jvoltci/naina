/* Material for MkDocs renders the search as role="dialog" with no name, which
 * fails aria-dialog-name (Lighthouse 2026-09-24). Give it one. */
document.addEventListener("DOMContentLoaded", function () {
  var s = document.querySelector(".md-search[role='dialog']");
  if (s && !s.getAttribute("aria-label")) s.setAttribute("aria-label", "Search");
});
