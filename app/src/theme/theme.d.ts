/* Types for theme.js, so TypeScript surfaces can import it. Copied with it. */
export declare const SUN: string;
export declare const MOON: string;
export declare function current(): "light" | "dark";
export declare function wireThemeToggles(scope?: ParentNode): void;
