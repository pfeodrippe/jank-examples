# Disco Elysium-Style Narrative Game Design

**Date**: 2026-02-04

## Overview

This document outlines the design for a Disco Elysium-inspired narrative game built with **jank + Vulkan** (no raylib). The game will feature:
- A vertical dialogue panel on the right side of the screen (~30% width)
- Rolling dialogue history that preserves past text
- Deeply nested dialogue choices
- Character-coded text with distinct colors
- Film-strip aesthetic with typewriter-style typography
- Chosen choices shown in a muted color vs. available choices in orange

## Story: La Voiture

A mysterious interactive fiction set in a foggy mangrove environment. Characters:
- **#V (Voiture)** - The mysterious waiting car with red lights
- **#M (Manguier)** - The mango trees, representing the environment/nature

The story explores themes of memory, waiting, and discovery through sparse, poetic French text.

---

## 1. Visual Design Specifications

### 1.1 Dialogue Panel Layout

```
+----------------------------------------+----------------------+
|                                        |   DIALOGUE PANEL     |
|                                        |   (~30% width)       |
|           SCENE VIEW                   |                      |
|       (70% width - left)               |  [Portrait Space]    |
|                                        |                      |
|   Isometric/atmospheric                |  SPEAKER NAME ---    |
|   environment rendering                |  Dialogue text...    |
|   using existing SDF engine            |                      |
|                                        |  SPEAKER2 NAME ---   |
|                                        |  More dialogue...    |
|                                        |                      |
|                                        |  [Choices at bottom] |
|                                        |  1. Choice one       |
|                                        |  2. Choice two       |
|                                        |  3. Leave [Leave.]   |
+----------------------------------------+----------------------+
```

### 1.2 Typography Specifications

| Element | Style | Color |
|---------|-------|-------|
| Speaker Labels | **BOLD ALL CAPS** + long dash (---) | Character-specific |
| Body Text | Serif typeface, regular weight | Light grey (#CCCCCC) |
| Narration | Same as body, but may have special styling | Slightly dimmer grey |
| Emphasis | *Italics* | Same as body |
| Player Choices | Numbered list | Burnt orange (#CC7733) |
| Chosen Choices | Same as choices | Muted grey (#666666) |
| Skill Checks | [Difficulty: Result] | Smaller, dimmer |

### 1.3 Color Coding by Character Type

```clojure
;; Character color definitions
(def character-colors
  {:voiture      {:name-color [0.9 0.3 0.3 1.0]   ;; Red - car/machine
                  :text-color [0.8 0.8 0.8 1.0]}
   :manguier     {:name-color [0.3 0.8 0.3 1.0]   ;; Green - nature/trees
                  :text-color [0.8 0.8 0.8 1.0]}
   :player       {:name-color [0.5 0.5 0.5 1.0]   ;; Dark grey - protagonist
                  :text-color [0.7 0.7 0.7 1.0]}
   :narrator     {:name-color [0.6 0.6 0.7 1.0]   ;; Blue-grey - narration
                  :text-color [0.8 0.8 0.8 1.0]}
   :skill        {:name-color [0.6 0.3 0.8 1.0]   ;; Purple - internal voice
                  :text-color [0.7 0.7 0.8 1.0]}})
```

### 1.4 Background Texture

- Semi-transparent dark charcoal (#1a1a1a at ~90% opacity)
- Film strip edge on far right with "KODAK SAFETY FILM" style markings
- Faded top edge where old dialogue scrolls out of view

---

## 2. Story Data Format

### 2.1 Markdown Format with YAML Frontmatter

Story files use Markdown with:
- YAML frontmatter for metadata (characters, settings)
- `#CHAR` prefix for speaker identification (e.g., `#V`, `#M`)
- `::` prefix for choices
- Indentation for nested content under choices

```markdown
---
title: "La Voiture"
author: "Unknown"
characters:
  V:
    name: "LA VOITURE"
    type: "object"
    color: "red"
    description: "A waiting car with red lights"
  M:
    name: "LES MANGUIERS"
    type: "nature"
    color: "green"
    description: "The surrounding mango trees"
settings:
  location: "Mangrove terrain"
  time: "Night, foggy"
  mood: "Mysterious, anticipatory"
---

#V Une voiture avec ses feux rouges attend quelqu'un...

:: Se retourner.
    #M Il n'existent que des manguiers aux alentours.

:: Inspecter la voiture.
    #V A la porte du conducteur, l'odeur de cigarette...
    
    :: Ouvrir la poignee.
        #V La poignee est froide et humide...
        
        :: Lire l'autocollant.
            Maman...
    
    :: Frapper a la porte.
        [Not yet implemented]
```

### 2.2 Parsed Data Structure

```clojure
;; Node types
{:type :dialogue   ;; Character speaks
 :speaker :voiture
 :text "Une voiture avec ses feux rouges..."
 :children [...]}  ;; Nested choices/content

{:type :choice     ;; Player choice
 :text "Se retourner."
 :selected? false  ;; Has player chosen this?
 :children [...]}  ;; Content revealed after choice

{:type :narration  ;; Pure narration (no speaker)
 :text "Things weren't going super well..."
 :children [...]}
```

---

## 3. Dialogue Engine Architecture

### 3.1 Core State

```clojure
(def dialogue-state
  (atom {:history []           ;; List of displayed nodes
         :current-node nil     ;; Current position in tree
         :available-choices [] ;; Choices currently available
         :selected-history []  ;; Track which choices were made
         :scroll-offset 0.0    ;; For scrolling through history
         :characters {}        ;; Character metadata from YAML
         :root-node nil}))     ;; The full dialogue tree
```

### 3.2 Key Functions

```clojure
;; Parse markdown story file
(defn parse-story-file [filepath]
  "Parse .md file into dialogue tree structure.")

;; Add dialogue to history
(defn append-to-history! [node]
  "Add a dialogue node to the visible history.")

;; Select a choice
(defn select-choice! [choice-idx]
  "Mark a choice as selected, reveal its children,
   add to history, update available choices.")

;; Get available choices
(defn get-current-choices []
  "Return currently available dialogue choices.")

;; Scroll history
(defn scroll-history! [delta]
  "Scroll up/down through dialogue history.")
```

### 3.3 Choice Flow

```
[Initial dialogue appears]
        |
        v
[Choices appear at bottom]
        |
    [Player clicks choice]
        |
        v
[Choice text added to history (muted color)]
        |
        v
[Content under choice revealed]
        |
        v
[New choices appear OR dialogue continues]
```

---

## 4. Vulkan Text Rendering

### 4.1 Approach: ImGui with Custom Styling

Since the project already uses ImGui with Vulkan, we'll leverage ImGui for text rendering with heavy customization:

```clojure
;; Custom styled text rendering
(defn render-speaker-name! [speaker-id text]
  (let [color (get-in character-colors [speaker-id :name-color])]
    (imgui/PushStyleColor imgui-h/ImGuiCol_Text 
                          (apply imgui-h/ImVec4. (map cpp/float. color)))
    (imgui/TextWrapped (str text " ---"))
    (imgui/PopStyleColor (cpp/int. 1))))

(defn render-dialogue-text! [text]
  (imgui/PushStyleColor imgui-h/ImGuiCol_Text 
                        (imgui-h/ImVec4. (cpp/float. 0.8) 
                                         (cpp/float. 0.8) 
                                         (cpp/float. 0.8) 
                                         (cpp/float. 1.0)))
  (imgui/TextWrapped text)
  (imgui/PopStyleColor (cpp/int. 1)))

(defn render-choice! [idx text selected?]
  (let [color (if selected? 
                [0.5 0.5 0.5 1.0]    ;; Muted
                [0.8 0.5 0.2 1.0])]  ;; Orange
    (imgui/PushStyleColor imgui-h/ImGuiCol_Text 
                          (apply imgui-h/ImVec4. (map cpp/float. color)))
    (when (imgui/Selectable (str (inc idx) ". " text))
      (select-choice! idx))
    (imgui/PopStyleColor (cpp/int. 1))))
```

### 4.2 Font Loading

Load a serif font (like EB Garamond or similar) for the literary feel:

```clojure
(defn load-dialogue-font! []
  (let [io (imgui/GetIO)
        fonts (cpp/.-Fonts io)]
    ;; Load primary dialogue font (serif, ~18pt)
    (cpp/.AddFontFromFileTTF fonts "fonts/EBGaramond-Regular.ttf" 
                             (cpp/float. 18.0))
    ;; Load bold variant for speaker names
    (cpp/.AddFontFromFileTTF fonts "fonts/EBGaramond-Bold.ttf"
                             (cpp/float. 18.0))))
```

### 4.3 Panel Background Rendering

Using ImGui window with custom styling:

```clojure
(defn render-dialogue-panel! []
  (let [window-width (sdfx/get_window_width)
        window-height (sdfx/get_window_height)
        panel-width (* window-width 0.30)
        panel-x (* window-width 0.70)]
    ;; Set panel position and size
    (imgui/SetNextWindowPos (imgui-h/ImVec2. (cpp/float. panel-x) 
                                              (cpp/float. 0.0)))
    (imgui/SetNextWindowSize (imgui-h/ImVec2. (cpp/float. panel-width)
                                               (cpp/float. window-height)))
    ;; Custom styling for dark, textured background
    (imgui/PushStyleColor imgui-h/ImGuiCol_WindowBg
                          (imgui-h/ImVec4. (cpp/float. 0.1)
                                           (cpp/float. 0.1)
                                           (cpp/float. 0.1)
                                           (cpp/float. 0.95)))
    (imgui/Begin "Dialogue" 
                 cpp/nullptr 
                 (bit-or imgui-h/ImGuiWindowFlags_NoTitleBar
                         imgui-h/ImGuiWindowFlags_NoResize
                         imgui-h/ImGuiWindowFlags_NoMove
                         imgui-h/ImGuiWindowFlags_NoScrollbar))
    ;; Render dialogue content here
    (render-dialogue-history!)
    (render-current-choices!)
    
    (imgui/End)
    (imgui/PopStyleColor (cpp/int. 1))))
```

---

## 5. Implementation Plan

### Phase 1: Story Parser
1. Create `vybe/narrative/parser.jank`
2. Parse YAML frontmatter for character metadata
3. Parse markdown content into dialogue tree
4. Handle nested indentation for choice hierarchies

### Phase 2: Dialogue State Management
1. Create `vybe/narrative/state.jank`
2. Implement atom-based state for dialogue history
3. Track choice selections and dialogue flow
4. Support undo/history navigation

### Phase 3: UI Rendering
1. Create `vybe/narrative/ui.jank`
2. Implement right-side panel with ImGui
3. Style text with character colors
4. Render choices with selection highlighting
5. Add scroll support for long dialogues

### Phase 4: Integration
1. Create `vybe/narrative.jank` main entry point
2. Integrate with existing SDF scene rendering
3. Handle keyboard/mouse input for choices
4. Add scene background (foggy mangrove atmosphere)

---

## 6. File Structure

```
src/vybe/
  narrative.jank          ;; Main entry point
  narrative/
    parser.jank           ;; Story file parsing
    state.jank            ;; Dialogue state management
    ui.jank               ;; ImGui-based UI rendering
    characters.jank       ;; Character color/style definitions

stories/
  la_voiture.md           ;; Main story file
  
fonts/
  EBGaramond-Regular.ttf  ;; Serif font for dialogue
  EBGaramond-Bold.ttf     ;; Bold variant for speaker names
  
vulkan_narrative/
  background.comp         ;; Shader for atmospheric background
```

---

## 7. Example Dialogue Rendering

```
+------------------------------------------+
|                                          |
| [Portrait: Car silhouette in fog]        |
|                                          |
| LA VOITURE ---                           |
| Une voiture avec ses feux rouges attend  |
| quelqu'un, il a air d'etre la il y a     |
| plusieurs heures. Le brouillard est      |
| epais, vous ne voyez rien d'autre que    |
| cette machine.                           |
|                                          |
| Vous sentez un souffle chaud derriere    |
| vous.                                    |
|                                          |
| ---------------------------------------- |
|                                          |
| 1. Se retourner.                         |
| 2. Inspecter la voiture.                 |
|                                          |
+------------------------------------------+
```

After selecting "Inspecter la voiture":

```
+------------------------------------------+
|                                          |
| LA VOITURE ---                           |
| Une voiture avec ses feux rouges...      |
| [scrolled up / faded]                    |
|                                          |
| 2. Inspecter la voiture.  [muted color]  |
|                                          |
| LA VOITURE ---                           |
| A la porte du conducteur, l'odeur de     |
| cigarette a l'orange envahit le silence. |
| Le terrain de mangrove recouvre vos      |
| semelles, c'est inconfortable, meme si   |
| vous y etes deja venu de nombreuses fois.|
|                                          |
| ---------------------------------------- |
|                                          |
| 1. Ouvrir la poignee.                    |
| 2. Frapper a la porte.                   |
|                                          |
+------------------------------------------+
```

---

## 8. Technical Considerations

### 8.1 jank/Vulkan Integration

- Use existing `sdfx` engine for window management
- Leverage ImGui integration for text rendering
- Scene background can use SDF shaders for atmosphere

### 8.2 Text Wrapping

ImGui's `TextWrapped` handles basic wrapping, but we may need custom logic for:
- Preserving French punctuation spacing
- Handling guillemets properly
- Line height adjustments

### 8.3 Performance

- Dialogue history is append-only (no dynamic tree modifications)
- Text rendering is cheap with ImGui
- SDF background can run at reduced frequency when not animating

### 8.4 Future Extensions

- Portrait images for characters (skill icons like Disco Elysium)
- Skill check dice rolls with animated results
- Multiple story files / chapters
- Save/load game state
- Internal monologue system (skills talking to player)

---

## 9. Dependencies

- jank with cpp interop
- Vulkan (already integrated)
- ImGui (already integrated)
- SDL3 (already integrated)
- Serif font files (need to add)

---

## 10. Commands to Run

```bash
# Run the narrative game
make narrative-run

# Or manually:
./bin/sdf-run narrative
```

---

## Next Steps

1. Create the story file `stories/la_voiture.md`
2. Implement parser for markdown + YAML
3. Create dialogue state management
4. Build ImGui-based UI
5. Integrate with main loop
