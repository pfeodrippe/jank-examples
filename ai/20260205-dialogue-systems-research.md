# Dialogue Systems Research: Disco Elysium & Beyond

**Date:** 2026-02-05

## Overview

Research into dialogue systems used in narrative games, with a focus on Disco Elysium's design, available authoring tools, structural patterns in choice-based games, and practical recommendations for our fiction app ("La Voiture").

---

## 1. Disco Elysium's Dialogue System — In Depth

### 1.1 Technical Foundation

- **Engine:** Unity
- **Dialogue Authoring:** articy:draft (professional narrative middleware)
- **Scale:** ~1.2 million words of dialogue across ~100 characters
- **Voice Acting:** The Final Cut added full voice acting; narrator Lenval Brown voiced approximately half the game's dialogue

### 1.2 Core Design: Skills as Internal Voices

The single most distinctive design innovation in DE is that the **24 skills function as internal dialogue characters**. Rather than skills being passive stat modifiers, each skill has:

- **A distinct personality and speaking voice** (e.g., Electrochemistry is hedonistic, Inland Empire is mystical, Authority is domineering)
- **Its own portrait icon** displayed in the dialogue panel
- **Active interjections** into conversations — skills interrupt, comment, contradict each other, and argue with the player

This creates a **"fragmented persona" effect** where the player character's mind is literally a committee of competing voices. The dialogue panel becomes a stage for internal debate.

**Example flow:**
```
LOGIC — The evidence doesn't add up. He's lying.
EMPATHY — But look at his hands. He's terrified.
AUTHORITY — Doesn't matter. Press him harder.

1. "I know you're lying."
2. "It's okay. Take your time."
3. [Drama - Challenging 12] Perform a dramatic accusation
```

Skills interject based on:
- The player's skill point allocation (higher = more frequent interjections)
- Context of the current conversation
- Game state and prior choices

### 1.3 Skill Checks

DE replaces combat entirely with **skill checks** — 2d6 dice rolls gated by skill levels:

- **Passive checks:** Happen automatically if skill is high enough (no roll)
- **Active checks:** Player chooses to attempt, rolls 2d6 + skill level vs. difficulty
- **White checks:** Can be retried after raising the skill
- **Red checks:** One chance only, permanent consequence

Difficulties range from Trivial (6) to Legendary (18). Modifiers come from:
- Equipment (e.g., wearing a specific hat)
- Thoughts internalized in the Thought Cabinet
- Prior dialogue choices and game state

### 1.4 The Thought Cabinet

A **secondary inventory system for ideas/concepts** rather than items:

- Unlocked through specific dialogue choices or observations
- Each thought takes real in-game time to "internalize" (research)
- While researching: temporary stat penalty
- Once internalized: permanent stat modifier (can be positive, negative, or mixed)
- Limited slots force the player to choose which ideas to keep
- Thematically brilliant — you're literally choosing what to think about

Examples: "Mazovian Socio-Economics" (a political ideology), "Finger on the Eject Button" (suicidal ideation), "Hobocop" (embracing homelessness).

### 1.5 Narrative Structure

DE uses a **branch-and-bottleneck** structure:

- Major plot beats are fixed (you must solve the murder case)
- Between bottleneck points, extensive branching allows different approaches
- Heavy **state-tracking** records hundreds of micro-decisions
- **"Microreactivity"**: small decisions propagate through the game world in subtle ways, rather than trying to create radically different storylines
- Kim Kitsuragi (companion) acts as a **narrative anchor** — his reactions to the player's behavior provide consistent feedback and emotional grounding

### 1.6 Influences

- Robert Kurvitz's own tabletop RPG system (the game's stat system derives from it)
- Planescape: Torment (dialogue-heavy RPG with unconventional setting)
- Fallout 1 & 2 (skill-check-gated dialogue)
- The Wire (systemic/institutional storytelling, flawed protagonists)

---

## 2. Dialogue Authoring Tools

### 2.1 ink (Inkle Studios)

- **Type:** Open-source narrative scripting language
- **License:** MIT
- **Philosophy:** Text-first markup, not a visual programming tool
- **Used in:** 80 Days, Sorcery! series, Heaven's Vault, Overboard!
- **Integration:** Unity plugin, Unreal plugin, compiles to JSON for any runtime
- **Key features:**
  - Weave syntax for inline branching (no explicit node management)
  - Knots and stitches for organizing content
  - Variable tracking and conditional content
  - Tunnels and threads for complex flow control
  - Designed for writers, not programmers

**Example ink syntax:**
```
=== interrogation ===
"Where were you last night?" you ask.

* "I was at home."
    "Can anyone confirm that?"
    ** "My wife can."
        -> wife_alibi
    ** "I live alone."
        ~ suspicion += 1
        -> press_harder
* "I don't have to answer that."
    ~ suspicion += 2
    -> lawyer_request
```

**Relevance to our project:** ink's text-first approach is closest to our custom Markdown format. We could potentially adopt ink-like syntax features (variables, conditionals) while keeping our Markdown base.

### 2.2 Yarn Spinner

- **Type:** Node-based dialogue system
- **Used in:** Night in the Woods, A Short Hike, DREDGE, NORCO
- **Integration:** Unity (primary), Godot, Unreal
- **Key features:**
  - Visual node editor for dialogue flow
  - Markup language for dialogue content within nodes
  - Command system for triggering game events from dialogue
  - Localization support built-in

### 2.3 Twine

- **Type:** Web-based hypertext interactive fiction tool
- **Output:** HTML
- **Best for:** Prototyping, web-based IF, non-programmers
- **Limitation:** Not designed for integration with game engines

### 2.4 articy:draft

- **Type:** Professional narrative middleware (commercial)
- **Used in:** Disco Elysium, Pillars of Eternity II, The Witcher (partial)
- **Key features:**
  - Visual flow editor for dialogue trees
  - Entity management (characters, items, locations)
  - Flow fragments with conditions and instructions
  - Export to various formats for engine integration
  - Team collaboration features
- **Cost:** Commercial license, significant investment

---

## 3. Standard Patterns in Choice-Based Games

Based on Sam Kabo Ashwell's taxonomy of interactive narrative structures:

### 3.1 Time Cave
- **Structure:** Heavily branching, no re-merging of paths
- **Result:** Exponential content growth, many short playthroughs
- **Encourages:** Replay to see different branches
- **Example:** Early Choose Your Own Adventure books
- **Weakness:** Most content unseen per playthrough; expensive to author

### 3.2 Gauntlet
- **Structure:** Linear central thread, branches are dead ends
- **Result:** Punishes wrong choices with failure/death
- **Example:** Many gamebooks, some visual novels
- **Weakness:** Choices feel fake — there's one "right" path

### 3.3 Branch and Bottleneck (DE uses this)
- **Structure:** Branches diverge but rejoin at key narrative events
- **Result:** Illusion of major divergence with manageable content scope
- **Key requirement:** Heavy state-tracking to make convergence feel natural
- **Example:** Disco Elysium, The Witcher series, most Bioware games
- **Strength:** Sustainable content creation while preserving meaningful choice

### 3.4 Quest
- **Structure:** Modular clusters organized by geography/topic
- **Result:** Player chooses order of engagement, not path
- **Example:** Open-world RPGs (Skyrim, Fallout)
- **Strength:** Content is reusable; player has agency over pacing

### 3.5 Open Map
- **Structure:** Reversible travel between nodes
- **Result:** Exploration-focused, choices about where to go
- **Example:** Zork, many parser IF games
- **Strength:** World feels explorable and persistent

### 3.6 Sorting Hat
- **Structure:** Early choice determines which major branch you experience
- **Result:** High replay value, distinct experiences per "path"
- **Example:** Visual novels with route selection
- **Weakness:** Early choice carries disproportionate weight

### 3.7 Floating Modules (Quality-Based Narrative)
- **Structure:** No trunk/spine; encounters appear based on accumulated state
- **Result:** Emergent narrative from state combinations
- **Example:** Failbetter Games (Fallen London, Sunless Sea), StoryNexus
- **Strength:** Highly replayable, state-driven, modular content creation
- **Relevance:** Could inspire our state-tracking approach

### 3.8 Loop and Grow
- **Structure:** Central loop repeats, but state unlocks new options each cycle
- **Result:** Familiar routine gradually transforms
- **Example:** Groundhog Day-like games, The Sexy Brutale, Outer Wilds
- **Strength:** Player learns through repetition; compact content with depth

---

## 4. Our Fiction App: Current State

### 4.1 Architecture

| Component | File | Role |
|-----------|------|------|
| Entry point | `src/fiction.jank` | Main loop, hot-reload |
| Parser | `src/fiction/parser.jank` | Custom Markdown -> dialogue tree |
| State | `src/fiction/state.jank` | ECS-backed state (history, choices, node stack) |
| Render bridge | `src/fiction/render.jank` | jank -> C++ render calls |
| Vulkan engine | `vulkan/fiction_engine.hpp` | SDL3 + Vulkan rendering |
| Text rendering | `vulkan/fiction_text.hpp` | stb_truetype text layout |
| Story file | `stories/la_voiture.md` | Custom Markdown + YAML frontmatter |

### 4.2 Current Story Format

```markdown
---
title: "La Voiture"
characters:
  V:
    name: "LA VOITURE"
    color: "red"
  M:
    name: "LES MANGUIERS"
    color: "green"
---

#∆V Une voiture avec ses feux rouges attend quelqu'un...

:: Se retourner.
    #∆M Il n'existent que des manguiers aux alentours.

:: Inspecter la voiture.
    #∆V A la porte du conducteur, l'odeur de cigarette...
    
    :: Ouvrir la poignee.
        #∆V La poignee est froide et humide...
```

Key format features:
- `#∆X` prefix for speaker lines (X = character code from frontmatter)
- `::` prefix for player choices
- Indentation-based nesting for hierarchy
- YAML frontmatter for character metadata

### 4.3 Parsed Data Structure

```clojure
{:type :dialogue, :speaker "V", :text "...", :children [...]}
{:type :choice, :text "...", :children [...]}
{:type :narration, :text "...", :children [...]}
```

### 4.4 Current Navigation

- Choices with nested sub-choices: navigate deeper (push stack)
- Choices without nested sub-choices: stay at current level
- Selected choices tracked and shown in muted grey
- History is append-only, scrollable

---

## 5. Recommendations for Evolution

### 5.1 Internal Voices System (High Impact, Medium Effort)

Adapt DE's skills-as-characters for our fiction app:

- Add a `type: "internal"` character type in frontmatter
- Internal characters represent aspects of the protagonist's psyche
- They interject into conversations using the same `#∆X` syntax
- Visual distinction: italic text, different color family (purples/blues)
- Could tie to a simple stat system that determines which voices speak up

```markdown
characters:
  I:
    name: "INTUITION"
    type: "internal"
    color: "purple"
    description: "Your gut feeling"
  R:
    name: "RAISON"
    type: "internal"
    color: "blue"
    description: "Your rational mind"
```

### 5.2 State Variables (High Impact, Medium Effort)

Add variable tracking to the story format:

```markdown
:: Ouvrir la poignee.
    [set: touched_car = true]
    [set: suspicion += 1]
    #∆V La poignee est froide et humide...

:: Frapper a la porte.
    [if: touched_car]
        #∆V Vos mains tremblent encore du froid de la poignee...
    [else]
        #∆V La porte vibre sous vos coups...
```

This enables:
- Conditional dialogue based on prior choices
- Microreactivity (small choices surfacing later)
- Skill check gates (see below)

### 5.3 Skill Checks (Medium Impact, Low Effort)

Simple dice-roll mechanic in the story format:

```markdown
:: [INTUITION - Difficile 14] Quelque chose ne va pas ici...
    [check: intuition >= 14]
    [success]
        #∆I Ce n'est pas du brouillard ordinaire. C'est de la fumee.
    [failure]
        #∆I ... Rien. L'intuition reste muette.
```

Implementation:
- Parse `[check: ...]` markers in the parser
- Roll 2d6 + relevant stat vs. difficulty
- Show success/failure content accordingly
- Display the roll result in the dialogue panel

### 5.4 Thought Cabinet Equivalent (Medium Impact, High Effort)

A "carnet de notes" (notebook) system:

- Specific dialogue choices unlock "pensees" (thoughts)
- Thoughts appear in a secondary panel/menu
- Each thought has a description and gameplay effect
- Internalizing a thought takes time (progresses with choices made)
- Once internalized, modifies available dialogue options or stat bonuses

### 5.5 Branch-and-Bottleneck Structure (Story Design)

For "La Voiture" specifically:

- Define 3-4 mandatory story beats (bottlenecks)
- Allow free exploration between them
- Use state-tracking to make convergence feel natural
- Kim-equivalent: the car itself (La Voiture) could be the narrative anchor that reacts to choices

### 5.6 Quality-Based Narrative Elements (Floating Modules)

Borrow from Failbetter's approach:

- Track "qualities" (e.g., `brouillard_knowledge`, `car_trust`, `fear_level`)
- Some story nodes only appear when qualities reach thresholds
- Enables emergent narrative without exponential branching
- Well-suited to our ECS architecture — qualities as components

### 5.7 Hot-Reload-Friendly Design

Our app already supports hot-reloading of story files. Lean into this:

- Story format should remain human-readable and diff-friendly
- Variables/state should serialize cleanly for save/load
- Parser should be resilient to partial/incomplete story files during authoring

---

## 6. Tool Comparison Matrix

| Feature | Our Format | ink | Yarn Spinner | articy:draft |
|---------|-----------|-----|-------------|-------------|
| Text-first authoring | Yes | Yes | Partial | No (visual) |
| Nested choices | Yes (indent) | Yes (weave) | Yes (nodes) | Yes (flow) |
| Variables/state | Not yet | Yes | Yes | Yes |
| Conditionals | Not yet | Yes | Yes | Yes |
| Skill checks | Not yet | Manual | Manual | Manual |
| Hot-reload | Yes | Partial | No | No |
| Version control friendly | Yes | Yes | Partial | No |
| Open source | Yes | Yes (MIT) | Yes (MIT) | No |
| Custom engine integration | Native | JSON export | Plugin | Export |

**Recommendation:** Don't switch to ink or Yarn Spinner. Instead, gradually add ink-inspired features (variables, conditionals) to our custom Markdown format. Our format's strengths — simplicity, hot-reload, version control friendliness — are worth preserving.

---

## 7. Implementation Priority

| Priority | Feature | Effort | Impact |
|----------|---------|--------|--------|
| 1 | State variables (`[set:]`, `[if:]`) | Medium | High — enables everything else |
| 2 | Internal voice characters | Medium | High — core DE-like experience |
| 3 | Simple skill checks | Low | Medium — adds gameplay tension |
| 4 | Sound/music triggers | Low | Medium — atmosphere |
| 5 | Thought cabinet / notebook | High | Medium — deepens engagement |
| 6 | Quality-based unlockable scenes | Medium | Medium — replayability |
| 7 | Save/load game state | Medium | High — required for longer stories |

---

## 8. Commands Used

```bash
make fiction          # Build and run the fiction app
```

## 9. What Was Learned

- DE's dialogue system is fundamentally about turning game mechanics (skills) into characters with voices — this is the key insight worth adapting
- The branch-and-bottleneck pattern with microreactivity is the most practical narrative structure for manageable content creation
- ink's syntax is the closest existing tool to our format and worth studying for feature inspiration
- Quality-based narrative (Failbetter style) maps well to ECS architecture
- Our custom Markdown format is a legitimate competitive advantage for author workflow — don't abandon it

## 10. Next Steps

- Implement state variables in the parser (`[set:]`, `[if:]` syntax)
- Add internal voice character type to frontmatter schema
- Prototype a simple skill check in the story
- Expand "La Voiture" story content to test branching depth
- Consider adding a WASM build for web demo
