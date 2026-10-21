---
name: qt-qml-ui
description: Build or modify Qt Quick/QML application UI, menus, inspectors, dialogs, keyboard interaction, selection presentation, or C++/QML boundaries. Use for user-interface work that is not engraving geometry itself.
---

# Qt/QML UI rules

QML is the presentation layer.

Use it for:
- menus
- toolbars
- transport controls
- inspectors
- dialogs
- panels
- keyboard shortcut routing
- lightweight transient view state

Do not use it as the canonical score model.

## Boundaries

Expose narrow intentional C++ APIs to QML.

Avoid:
- exposing huge mutable C++ object graphs directly
- business logic hidden in JavaScript snippets
- duplicating canonical state in QML
- thousands of QML notation objects

The score viewport should be a dedicated high-performance rendering component.

## Interaction

Input handling should resolve user gestures into application commands, for example:

```text
click geometry
-> hit-test stable object ID
-> selection command/state
-> edit command
-> model mutation
-> invalidation
-> updated render snapshot
```

Keep hit testing and musical mutation distinct.

During playback, disable editing controls; the application/edit layer also rejects edit commands until playback stops.

## Responsiveness

Never perform long layout, file parsing, Python analysis, or blocking device work directly from a UI handler.

## Definition of done

- keyboard and pointer path works
- focus behavior is intentional
- accessibility/labels are added where applicable
- UI does not become a new source of musical truth
