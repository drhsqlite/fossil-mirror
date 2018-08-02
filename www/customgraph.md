# Customizing the Timeline Graph

Beginning with version 1.33, Fossil gives users and skin authors significantly
more control over the look and feel of the timeline graph.

## <a id="basic-style"></a>Basic Style Options

Fossil includes several options for changing the graph's style without having
to delve into CSS. These can be found in the details.txt file of your skin or
under Admin/Skins/Details in the web UI.

*   ###`timeline-arrowheads`

    Set this to `0` to hide arrowheads on primary child lines.

*   ###`timeline-circle-nodes`

    Set this to `1` to make check-in nodes circular instead of square.

*   ###`timeline-color-graph-lines`

    Set this to `1` to colorize primary child lines.

*   ###`white-foreground`

    Set this to `1` if your skin uses white (or any light color) text.
    This tells Fossil to generate darker background colors for branches.


## <a id="adv-style"></a>Advanced Styling

If the above options aren't enough for you, it's time to get your hands dirty
with CSS. To get started, I recommend first copying all the [graph-related CSS
rules](#default-css) to your stylesheet. Then it's simply a matter of making
the necessary changes to achieve the look you want. So, next, let's look at the
various graph elements and what purpose they serve.

Each element used to construct the timeline graph falls into one of two
categories: visible elements and positioning elements. We'll start with the
latter, less obvious type.

## <a id="pos-elems"></a>Positioning Elements

These elements aren't intended to be seen. They're only used to help position
the graph and its visible elements.

*   ###<a id="tl-canvas"></a>`.tl-canvas`

    Set the left and right margins on this class to give the desired amount
    of space between the graph and its adjacent columns in the timeline.

    #### Additional Classes

    * `.sel`: See [`.tl-node`](#tl-node) for more information.

*   ###<a id="tl-rail"></a>`.tl-rail`

    Think of rails as invisible vertical lines on which check-in nodes are
    placed. The more simultaneous branches in a graph, the more rails required
    to draw it. Setting the `width` property on this class determines the
    maximum spacing between rails. This spacing is automatically reduced as
    the number of rails increases. If you change the `width` of `.tl-node`
    elements, you'll probably need to change this value, too.

*   ###<a id="tl-mergeoffset"></a>`.tl-mergeoffset`

    A merge line often runs vertically right beside a primary child line. This
    class's `width` property specifies the maximum spacing between the two.
    Setting this value to `0` will eliminate the vertical merge lines.
    Instead, the merge arrow will extend directly off the primary child line.
    As with rail spacing, this is also adjusted automatically as needed.

*   ###<a id="tl-nodemark"></a>`.tl-nodemark`

    In the timeline table, the second cell in each check-in row contains an
    invisible div with this class. These divs are used to determine the
    vertical position of the nodes. By setting the `margin-top` property,
    you can adjust this position.

## <a id="vis-elems"></a>Visible Elements

These are the elements you can actually see on the timeline graph: the nodes,
arrows, and lines. Each of these elements may also have additional classes
attached to them, depending on their context.

*   ###<a id="tl-node"></a>`.tl-node`

    A node exists for each check-in in the timeline.

    #### Additional Classes

    *   `.leaf`: Specifies that the check-in is a leaf (i.e. that it has no
        children in the same branch).

    *   `.merge`: Specifies that the check-in contains a merge.

    *   `.sel`: When the user clicks a node to designate it as the beginning
        of a diff, this class is added to both the node itself and the
        [`.tl-canvas`](#tl-canvas) element. The class is removed from both
        elements when the node is clicked again.

*   ###<a id="tl-arrow"></a>`.tl-arrow`

    Arrows point from parent nodes to their children. Technically, this
    class is just for the arrowhead. The rest of the arrow is composed
    of [`.tl-line`](#tl-line) elements.

    There are six additional classes that are used to distinguish the different
    types of arrows. However, only these combinations are valid:

    *   `.u`: Up arrow that points to a child from its primary parent.

    *   `.u.sm`: Smaller up arrow, used when there is limited space between
        parent and child nodes.

    *   `.merge.l` or `.merge.r`: Merge arrow pointing either to the left or
        right.

    *   `.warp`: A timewarped arrow (always points to the right), used when a
        misconfigured clock makes a check-in appear to have occurred before its
        parent ([example](https://www.sqlite.org/src/timeline?c=2010-09-29&nd)).

*   ###<a id="tl-line"></a>`.tl-line`

    Along with arrows, lines connect parent and child nodes. Line thickness is
    determined by the `width` property, regardless of whether the line is
    horizontal or vertical. You can also use borders to create special line
    styles. Here's a CSS snippet for making dotted merge lines:

        .tl-line.merge {
          width: 0;
          background: transparent;
          border: 0 dotted #000;
        }
        .tl-line.merge.h {
          border-top-width: 1px;
        }
        .tl-line.merge.v {
          border-left-width: 1px;
        }

    #### Additional Classes

    *   `.merge`: A merge line.

    *   `.h` or `.v`: Horizontal or vertical.

    *   `.warp`: A timewarped line.


## <a id="default-css"></a>Default Timeline Graph CSS

    .tl-canvas {
      margin: 0 6px 0 10px;
    }
    .tl-rail {
      width: 18px;
    }
    .tl-mergeoffset {
      width: 2px;
    }
    .tl-nodemark {
      margin-top: 5px;
    }
    .tl-node {
      width: 10px;
      height: 10px;
      border: 1px solid #000;
      background: #fff;
      cursor: pointer;
    }
    .tl-node.leaf:after {
      content: '';
      position: absolute;
      top: 3px;
      left: 3px;
      width: 4px;
      height: 4px;
      background: #000;
    }
    .tl-node.sel:after {
      content: '';
      position: absolute;
      top: 2px;
      left: 2px;
      width: 6px;
      height: 6px;
      background: red;
    }
    .tl-arrow {
      width: 0;
      height: 0;
      transform: scale(.999);
      border: 0 solid transparent;
    }
    .tl-arrow.u {
      margin-top: -1px;
      border-width: 0 3px;
      border-bottom: 7px solid #000;
    }
    .tl-arrow.u.sm {
      border-bottom: 5px solid #000;
    }
    .tl-line {
      background: #000;
      width: 2px;
    }
    .tl-arrow.merge {
      height: 1px;
      border-width: 2px 0;
    }
    .tl-arrow.merge.l {
      border-right: 3px solid #000;
    }
    .tl-arrow.merge.r {
      border-left: 3px solid #000;
    }
    .tl-line.merge {
      width: 1px;
    }
    .tl-arrow.warp {
      margin-left: 1px;
      border-width: 3px 0;
      border-left: 7px solid #600000;
    }
    .tl-line.warp {
      background: #600000;
    }
