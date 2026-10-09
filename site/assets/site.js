(() => {
  const root = document.documentElement;
  const themeToggle = document.querySelector(".theme-toggle");
  const storedTheme = (() => {
    try {
      return localStorage.getItem("simpl-theme");
    } catch {
      return null;
    }
  })();
  const initialTheme = storedTheme === "dark" || storedTheme === "light"
    ? storedTheme
    : "light";

  root.dataset.theme = initialTheme;

  const updateThemeToggle = () => {
    if (!themeToggle) return;
    const nextTheme = root.dataset.theme === "dark" ? "light" : "dark";
    const label = `Включить ${nextTheme === "dark" ? "тёмную" : "светлую"} тему`;
    themeToggle.textContent = nextTheme === "dark" ? "☾" : "☀";
    themeToggle.setAttribute("aria-label", label);
    themeToggle.title = label;
  };

  updateThemeToggle();
  themeToggle?.addEventListener("click", () => {
    const nextTheme = root.dataset.theme === "dark" ? "light" : "dark";
    root.dataset.theme = nextTheme;
    try {
      localStorage.setItem("simpl-theme", nextTheme);
    } catch {
      // The selected theme still applies for this page if storage is unavailable.
    }
    updateThemeToggle();
  });

  const toggle = document.querySelector(".menu-toggle");
  const nav = document.querySelector(".site-nav");

  if (toggle && nav) {
    toggle.addEventListener("click", () => {
      const isOpen = toggle.getAttribute("aria-expanded") === "true";
      toggle.setAttribute("aria-expanded", String(!isOpen));
      nav.classList.toggle("is-open", !isOpen);
    });
  }
})();
