/* Altrusian theme switch. Copied whole into every surface that has a header.
 *
 * Two parts. The first line runs in <head> before paint, inlined, so a saved
 * choice never flashes the other mode:
 *
 *   <script>try{var t=localStorage.getItem("theme");if(t==="light"||t==="dark")document.documentElement.classList.add(t)}catch(e){}</script>
 *
 * The second wires any button with [data-theme-toggle]: it flips to the mode
 * that is not showing, remembers it, and a second click back to the OS mode
 * forgets the choice. Icons are Lucide sun and moon (ISC), 24 grid, stroke 2.
 */
export const SUN = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/></svg>';
export const MOON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401"/></svg>';

/* Read lazily: this module is also imported by server-rendered pages (Next), where
 * there is no document until the browser runs it. */
const html = () => document.documentElement;
const os = () => (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
export const current = () => (html().classList.contains("dark") ? "dark" : html().classList.contains("light") ? "light" : os());

function paint(btn) {
  const dark = current() === "dark";
  btn.innerHTML = dark ? SUN : MOON;
  btn.setAttribute("aria-label", dark ? "Switch to light mode" : "Switch to dark mode");
  btn.title = btn.getAttribute("aria-label");
}

export function wireThemeToggles(scope = document) {
  const buttons = [...scope.querySelectorAll("[data-theme-toggle]")];
  for (const btn of buttons) {
    paint(btn);
    btn.addEventListener("click", () => {
      const next = current() === "dark" ? "light" : "dark";
      const root = html();
      root.classList.remove("light", "dark");
      try {
        if (next === os()) localStorage.removeItem("theme");
        else { root.classList.add(next); localStorage.setItem("theme", next); }
      } catch { root.classList.add(next); }
      buttons.forEach(paint);
    });
  }
  matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => buttons.forEach(paint));
}
