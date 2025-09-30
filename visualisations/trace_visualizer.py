#!/usr/bin/env python3
"""Grid-based visualization for arbiter trace.

Renders the trace as a sequence of frames showing signal states and grant status.
"""

import re
from pathlib import Path

import matplotlib.patches as patches
import matplotlib.pyplot as plt

# import numpy as np
from matplotlib.animation import FuncAnimation, PillowWriter


class ArbiterTraceVisualizer:
    def __init__(self, trace_file, output_dir="output"):
        self.trace_file = Path(trace_file)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)

        self.frames = []
        self.parse_trace()

        # Grid dimensions
        self.grid_size = 3  # 3x3 grid
        self.cell_size = 1.0

        # Colors
        self.colors = {
            "signal_on": "#2E7D32",  # Green for signal on
            "signal_off": "#B71C1C",  # Red for signal off
            "grant_on": "#FFD700",  # Gold for grant on
            "grant_off": "#9E9E9E",  # Gray for grant off
            "background": "#F5F5F5",  # Light gray background
            "grid": "#BDBDBD",  # Grid lines
        }

    def parse_trace(self):
        """Parse the trace file to extract frames."""
        with open(self.trace_file, "r") as f:
            content = f.read().strip()

        # Parse the trace format: !g_0&s_0;!g_0&s_0;...
        # Extract the first line with actual trace data
        for line in content.split("\n"):
            if "&" in line and ";" in line:
                # Remove any leading numbers and arrows
                trace_data = re.sub(r"^[0-9→\s]+", "", line)
                # Remove cycle information at the end
                trace_data = re.sub(r";cycle\{[0-9]+\}$", "", trace_data)

                # Split into individual frames
                frame_strings = trace_data.split(";")

                for frame_str in frame_strings:
                    if frame_str.strip():
                        frame = self.parse_frame(frame_str.strip())
                        self.frames.append(frame)
                break

    def parse_frame(self, frame_str):
        """Parse a single frame string like '!g_0&s_0' into state dict."""
        state = {"s_0": False, "g_0": False}

        # Split by & to get individual literals
        literals = frame_str.split("&")

        for literal in literals:
            literal = literal.strip()
            if literal.startswith("!"):
                # Negated literal
                var = literal[1:]
                if var in state:
                    state[var] = False
            else:
                # Positive literal
                if literal in state:
                    state[literal] = True

        return state

    def draw_frame(self, ax, frame_idx):
        """Draw a single frame on the given axes."""
        ax.clear()
        ax.set_xlim(-0.5, self.grid_size - 0.5)
        ax.set_ylim(-0.5, self.grid_size - 0.5)
        ax.set_aspect("equal")

        # Remove ticks
        ax.set_xticks([])
        ax.set_yticks([])

        # Set background
        ax.set_facecolor(self.colors["background"])

        # Get current frame state
        if frame_idx < len(self.frames):
            state = self.frames[frame_idx]
        else:
            state = {"s_0": False, "g_0": False}

        # Draw grid cells
        # Top row: Signal indicator
        signal_color = (
            self.colors["signal_on"] if state["s_0"] else self.colors["signal_off"]
        )
        rect = patches.Rectangle(
            (0.5, 1.5),
            1,
            1,
            linewidth=2,
            edgecolor=self.colors["grid"],
            facecolor=signal_color,
            alpha=0.8,
        )
        ax.add_patch(rect)
        ax.text(
            1,
            2,
            "s_0",
            ha="center",
            va="center",
            fontsize=14,
            fontweight="bold",
            color="white",
        )

        # Middle: Arrow indicating request/grant flow
        if state["s_0"]:
            arrow = patches.FancyArrowPatch(
                (1, 1.4),
                (1, 0.6),
                connectionstyle="arc3",
                arrowstyle="->",
                mutation_scale=20,
                color="black",
                linewidth=2,
            )
            ax.add_patch(arrow)

        # Bottom row: Grant indicator
        grant_color = (
            self.colors["grant_on"] if state["g_0"] else self.colors["grant_off"]
        )
        rect = patches.Rectangle(
            (0.5, -0.5),
            1,
            1,
            linewidth=2,
            edgecolor=self.colors["grid"],
            facecolor=grant_color,
            alpha=0.8,
        )
        ax.add_patch(rect)
        ax.text(
            1,
            0,
            "g_0",
            ha="center",
            va="center",
            fontsize=14,
            fontweight="bold",
            color="black" if state["g_0"] else "white",
        )

        # Add labels
        ax.text(2.2, 2, "Signal", ha="left", va="center", fontsize=12)
        ax.text(2.2, 0, "Grant", ha="left", va="center", fontsize=12)

        # Add frame number and state info
        ax.text(
            1,
            -1.2,
            f"Frame {frame_idx + 1}/{len(self.frames)}",
            ha="center",
            va="center",
            fontsize=11,
        )

        state_text = (
            f"s_0={'ON' if state['s_0'] else 'OFF'},"
            f"g_0={'ON' if state['g_0'] else 'OFF'}",
        )
        ax.text(
            1, -1.5, state_text, ha="center", va="center", fontsize=10, style="italic"
        )

        # Add title
        ax.text(
            1,
            2.8,
            "Arbiter State",
            ha="center",
            va="center",
            fontsize=16,
            fontweight="bold",
        )

    def save_frames(self):
        """Save individual frames as images."""
        frames_dir = self.output_dir / "frames"
        frames_dir.mkdir(exist_ok=True)

        for idx, frame in enumerate(self.frames):
            fig, ax = plt.subplots(1, 1, figsize=(6, 6))
            self.draw_frame(ax, idx)

            filename = frames_dir / f"frame_{idx:03d}.png"
            plt.savefig(filename, dpi=100, bbox_inches="tight", facecolor="white")
            plt.close()

        print(f"Saved {len(self.frames)} frames to {frames_dir}")

    def create_animation(self, fps=2):
        """Create an animated GIF of the trace."""
        fig, ax = plt.subplots(1, 1, figsize=(6, 6))

        def update(frame_idx):
            self.draw_frame(ax, frame_idx)
            return ax.patches + ax.texts

        anim = FuncAnimation(
            fig, update, frames=len(self.frames), interval=1000 / fps, repeat=True
        )

        # Save as GIF
        gif_path = self.output_dir / "trace_animation.gif"
        writer = PillowWriter(fps=fps)
        anim.save(gif_path, writer=writer)
        plt.close()

        print(f"Saved animation to {gif_path}")

    def create_grid_view(self, cols=3):
        """Create a grid view showing multiple frames at once."""
        n_frames = len(self.frames)
        rows = (n_frames + cols - 1) // cols

        fig, axes = plt.subplots(rows, cols, figsize=(cols * 4, rows * 4))

        # Flatten axes array for easy iteration
        if rows == 1:
            axes = axes.reshape(1, -1)
        if cols == 1:
            axes = axes.reshape(-1, 1)

        for idx in range(rows * cols):
            row = idx // cols
            col = idx % cols
            ax = axes[row, col]

            if idx < n_frames:
                self.draw_frame(ax, idx)
            else:
                ax.axis("off")

        plt.suptitle("Arbiter Trace Sequence", fontsize=18, fontweight="bold")
        plt.tight_layout()

        # Save grid view
        grid_path = self.output_dir / "trace_grid.png"
        plt.savefig(grid_path, dpi=150, bbox_inches="tight", facecolor="white")
        plt.close()

        print(f"Saved grid view to {grid_path}")

    def print_trace_summary(self):
        """Print a summary of the trace."""
        print("\n" + "=" * 50)
        print("TRACE SUMMARY")
        print("=" * 50)
        print(f"Total frames: {len(self.frames)}")
        print("\nFrame-by-frame states:")
        print("-" * 30)

        for idx, frame in enumerate(self.frames):
            s_state = "ON " if frame["s_0"] else "OFF"
            g_state = "ON " if frame["g_0"] else "OFF"
            print(f"Frame {idx+1:2d}: Signal={s_state}  Grant={g_state}")

        # Analyze patterns
        print("\n" + "-" * 30)
        print("PATTERN ANALYSIS:")

        # Count state changes
        signal_changes = 0
        grant_changes = 0

        for i in range(1, len(self.frames)):
            if self.frames[i]["s_0"] != self.frames[i - 1]["s_0"]:
                signal_changes += 1
            if self.frames[i]["g_0"] != self.frames[i - 1]["g_0"]:
                grant_changes += 1

        print(f"Signal changes: {signal_changes}")
        print(f"Grant changes: {grant_changes}")

        # Check for grant-follows-signal pattern
        grant_after_signal = 0
        for i in range(len(self.frames) - 1):
            if (
                not self.frames[i]["s_0"] and self.frames[i + 1]["s_0"]
            ):  # Signal turns on
                # Look ahead for grant
                for j in range(i + 1, min(i + 5, len(self.frames))):
                    if self.frames[j]["g_0"]:
                        grant_after_signal += 1
                        break

        print(f"Times grant followed signal change: {grant_after_signal}")
        print("=" * 50 + "\n")


def main():
    """Main function to run the visualizer."""
    import argparse

    parser = argparse.ArgumentParser(description="Visualize arbiter trace")
    parser.add_argument(
        "--trace",
        default="output/input.trace",
        help="Path to trace file (default: output/input.trace)",
    )
    parser.add_argument(
        "--output",
        default="output/visualization",
        help="Output directory (default: output/visualization)",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=2,
        help="Frames per second for animation (default: 2)",
    )
    parser.add_argument(
        "--grid-cols",
        type=int,
        default=3,
        help="Number of columns in grid view (default: 3)",
    )
    parser.add_argument(
        "--no-frames", action="store_true", help="Skip saving individual frames"
    )
    parser.add_argument(
        "--no-animation", action="store_true", help="Skip creating animation"
    )
    parser.add_argument(
        "--no-grid", action="store_true", help="Skip creating grid view"
    )

    args = parser.parse_args()

    # Create visualizer
    viz = ArbiterTraceVisualizer(args.trace, args.output)

    # Print summary
    viz.print_trace_summary()

    # Generate visualizations
    if not args.no_frames:
        print("\nGenerating individual frames...")
        viz.save_frames()

    if not args.no_animation:
        print("\nCreating animation...")
        viz.create_animation(fps=args.fps)

    if not args.no_grid:
        print("\nCreating grid view...")
        viz.create_grid_view(cols=args.grid_cols)

    print("\nVisualization complete!")


if __name__ == "__main__":
    main()
