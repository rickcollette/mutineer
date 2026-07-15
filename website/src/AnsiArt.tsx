type AnsiSpan = {
  text: string;
  foreground?: string;
  background?: string;
};

type AnsiStyle = {
  foreground?: string;
  background?: string;
};

type AnsiArtProps = {
  content: string;
  className?: string;
};

function rgb(values: string[], start: number) {
  return `rgb(${values[start]}, ${values[start + 1]}, ${values[start + 2]})`;
}

function applySgr(style: AnsiStyle, sequence: string) {
  const codes = sequence.length > 0 ? sequence.split(';') : ['0'];
  const next = { ...style };

  for (let i = 0; i < codes.length; i += 1) {
    const code = codes[i];

    if (code === '0') {
      delete next.foreground;
      delete next.background;
    } else if (code === '39') {
      delete next.foreground;
    } else if (code === '49') {
      delete next.background;
    } else if ((code === '38' || code === '48') && codes[i + 1] === '2' && i + 4 < codes.length) {
      const color = rgb(codes, i + 2);
      if (code === '38') {
        next.foreground = color;
      } else {
        next.background = color;
      }
      i += 4;
    }
  }

  return next;
}

function sameStyle(left: AnsiStyle, right: AnsiStyle) {
  return left.foreground === right.foreground && left.background === right.background;
}

function parseAnsi(content: string) {
  const spans: AnsiSpan[] = [];
  const pattern = /\x1b\[([0-9;]*)m/g;
  let style: AnsiStyle = {};
  let lastIndex = 0;
  let match: RegExpExecArray | null;

  while ((match = pattern.exec(content)) !== null) {
    const text = content.slice(lastIndex, match.index);
    if (text.length > 0) {
      const previous = spans[spans.length - 1];
      if (previous && sameStyle(previous, style)) {
        previous.text += text;
      } else {
        spans.push({ text, ...style });
      }
    }

    style = applySgr(style, match[1]);
    lastIndex = pattern.lastIndex;
  }

  const tail = content.slice(lastIndex);
  if (tail.length > 0) {
    spans.push({ text: tail, ...style });
  }

  return spans;
}

export default function AnsiArt({ content, className }: AnsiArtProps) {
  const spans = parseAnsi(content);

  return (
    <pre className={className} aria-label="Mutineer ANSI art">
      {spans.map((span, index) => (
        <span
          key={`${index}-${span.text.length}`}
          style={{ color: span.foreground, backgroundColor: span.background }}
        >
          {span.text}
        </span>
      ))}
    </pre>
  );
}
