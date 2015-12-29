#include <stdio.h>
#include <windows.h>
#include <windowsx.h>

void convertFont(const char *inName, const char *outName) {
	FILE *f = fopen(inName, "rb");
	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *data = new char[size];
	fread(data, 1, size, f);
	fclose(f);

	struct Header {
		char	size;
		char	info[11];
		short	width;
		short	height;
		char	bpp;
		char	imgInfo;
	} *tga = (Header*)data;
	

	if (tga->width != 128 || tga->height != 256 || tga->bpp != 32) {
		printf("! wrong %s format\n", inName);
		delete data;
		return;
	}

	struct RGBA {
		unsigned char r, g, b, a;
	} *pix = (RGBA*)&data[sizeof(Header) + tga->size];
	
	int count = 256 * 16;
	unsigned char *res = new unsigned char[count];
	for (int i = 0; i < 256; i++) {
		int ox = (i % 16) * 8;
		int oy = (i / 16) * 16;

		for (int y = 0; y < 16; y++) {
			unsigned char &r = res[i * 16 + y] = 0;

			for (int x = 0; x < 8; x++) {
				int ix = ox + x;
				int iy = oy + y;
				r |= (pix[ix + iy * 128].r > 0) << x;
			}
		}
	}

	f = fopen(outName, "wb");
	fwrite(res, sizeof(res[0]), count, f);
	fclose(f);

	delete data;
	delete res;

	printf("convert %s -> %s\n", inName, outName);
}

typedef unsigned int Color;

#define COLOR_CLEAR	0xFF000000

struct Point {
	int x, y;
	Point() {}
	Point(int x, int y) : x(x), y(y) {}
};

struct Rect {
	int l, t, r, b;
};

struct Font {
	unsigned char *data;

	Font(const char *name) {
		FILE *f = fopen(name, "rb");
		int rows = 256 * 16;
		data = new unsigned char[rows];
		fread(data, sizeof(data[0]), rows, f);
		fclose(f);
	}

	~Font() {
		delete data;
	}

	void putChar(unsigned char c, Color fColor, Color bColor, Color *pixel, int stride) {
		unsigned char *d = &data[c * 16];

		for (int y = 0; y < 16; y++) {
			unsigned char v = d[y];

			for (int x = 0; x < 9; x++) {
				if (x == 8 && c >= 0xC0 && c <= 0xDF)
					v = d[y] >> 7;

				if (v & 1)
					pixel[x] = fColor;
				else
					if (bColor != COLOR_CLEAR)
						pixel[x] = bColor;
				v >>= 1;
			}

			pixel = &pixel[stride];
		}
	}
};

struct Canvas {
	int		width, height;
	Color	*pixels;
	Font	*font;
	Rect	rect;

	Canvas() : width(0), height(0), pixels(NULL) {
		font = new Font("font.dat");
	}

	~Canvas() {
		delete font;
		if (pixels) free(pixels);
	}

	void resize(int width, int height) {
		if (this->width != width || this->height != height) {
			this->width = width;
			this->height = height;
			pixels = (Color*)realloc(pixels, width * height * 4);
		}
	}

	void present(HDC dc) {
		if (rect.l > rect.r || rect.t > rect.b)
			return;
	//	RECT r = { 0, 0, width, height };
	//	FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
		BITMAPINFO bmi = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
		SetDIBitsToDevice(dc, rect.l, rect.t, rect.r - rect.l, rect.b - rect.t, rect.l, rect.t, rect.t, rect.b, pixels, &bmi, 0);
	//	SetDIBitsToDevice(dc, 0, 0, width, height, 0, 0, 0, height, pixels, &bmi, 0);
	}

	void clear(Color color) {
		for (int i = 0; i < width * height; i++)
			pixels[i] = color;
	}

	void scrollX(int delta) {

	}

	void scrollY(int offset) {
		int size = width * (height - abs(offset)) * 4;
		if (size <= 0) return;

		if (offset > 0)
			memmove(&pixels[offset * width], &pixels[0], size);
		else
			memmove(&pixels[0], &pixels[-offset * width], size);
	}

	void fill(int x, int y, int w, int h, Color color) {
		for (int j = y; j < y + h; j++)
			for (int i = x; i < x + w; i++)
				pixels[i + j * width] = color;
	};

	Point print(int ox, int x, int y, Color fColor, Color bColor, const char *text) {
		if (text) {
			const char *c = &text[0];
			while (*c) {
				switch (*c) {
					case '\n' :
						break;
					case '\r' :
						y += 16;
						x = 0;
						break;
					case '\t' :
						x = (x / (9 * 4) + 1) * (9 * 4);
						break;
					default :
						if (ox + x >= 0 && ox + x < width - 9 && y >= 0 && y < height - 16)
							font->putChar(*c, fColor, bColor, &pixels[ox + x + y * width], width);
						x += 9;
				}
				c++;
			}
		}
		return Point(x, y);
	}
};

struct Editor {
private:
	char	*text;
	Point	cursor;
	Point	scroll;
	Point	offset;
	bool	valid;
public:
	enum ThemeColor {
		COLOR_CODE,
		COLOR_TEXT,
		COLOR_TYPE,
		COLOR_DEFINE,
		COLOR_NUMBER,
		COLOR_OPCODE,
		COLOR_COMMENT,
		COLOR_ARGUMENT,
		COLOR_BACK_LINE,
		COLOR_BACK_NORMAL,
		COLOR_BACK_SELECTION,
		COLOR_BACK_SEARCH,
		COLOR_CURSOR,
		COLOR_MAX
	};

	struct Theme {
		union {
			struct {
				Color byID[COLOR_MAX];
			};
			struct {
				Color code;
				Color text;
				Color type;
				Color define;
				Color number;
				Color opcode;
				Color comment;
				Color argument;
				Color back_line;
				Color back_normal;
				Color back_selection;
				Color back_search;
				Color cursor;
			};
		};
	} theme;

	struct Syntax {

		struct Lexeme {
			enum ID { 
				ID_CODE, ID_TEXT, ID_TYPE, ID_DEFINE, ID_NUMBER, ID_OPCODE, ID_COMMENT, ID_ARGUMENT, ID_MAX = 0xFFFFFFFF
			} id;
			int	offset;
			int	length;
		} *lexeme;
		int count;

		Syntax() : lexeme(NULL) {}
		
		~Syntax() { 
			if (lexeme) free(lexeme);
		}

		void lexemeBegin(int pos, Lexeme::ID id) {
			if (count && !lexeme[count - 1].length)
				return;

			lexeme = (Lexeme*)realloc(lexeme, (count + 1) * sizeof(Lexeme));
			lexeme[count].id		= id;
			lexeme[count].offset	= pos;
			lexeme[count].length	= 0;
			count++; 
		};

		void lexemeEnd(int pos) {
			if (count && !lexeme[count - 1].length)
				lexeme[count - 1].length = pos - lexeme[count - 1].offset;
		};

		bool checkOpcode(const char *str) {
			static char *opcodes[] = {	"void", "char", "bool", "short", "int", "long", "float", "double", "this", "typedef", "unsigned", "enum",
										"sizeof", "return", "const", "static", "struct", "public", "private", "protected", "virtual", "new", "delete",
										"for", "while", "do", "true", "false", "if", "else", "continue", "break", "switch", "case", "default" };

			for (int i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); i++)
				if (!strcmp(str, opcodes[i]))
					return true;
			return false;
		}

		bool checkArgument(const char *str) {
			static char *args[] = { "#include", "#define", "#undef", "#if", "#ifdef", "#ifndef", "#else", "#endif" };
			for (int i = 0; i < sizeof(args) / sizeof(args[0]); i++)
				if (!strcmp(str, args[i]))
					return true;
			return false;
		}
		
		bool checkDefine(const char *str) {
			static char *defines[] = {	"NULL", "SEEK_END", "SEEK_CUR", "SEEK_SET", "COLOR_CLEAR", "VK_LEFT", "VK_RIGHT", "VK_UP", "VK_DOWN", "VK_BACK", "CALLBACK", "GetWindowLong", "SetWindowLong", "WIN32", "_DEBUG",
										"GWL_USERDATA", "GWL_WNDPROC", "LOWORD", "HIWORD", "GET_WHEEL_DELTA_WPARAM", "GET_X_LPARAM", "GET_Y_LPARAM", "WM_PAINT", "WM_SIZE", "WM_KEYDOWN", "WM_CHAR", "WM_MOUSEWHEEL", 
										"WM_LBUTTONDOWN", "WM_LBUTTONUP", "WM_RBUTTONDOWN", "WM_RBUTTONUP", "WM_DESTROY", "DefWindowProc", "CreateWindow", "WS_OVERLAPPEDWINDOW", "SW_SHOWDEFAULT", "GetMessage", "DispatchMessage" };
			for (int i = 0; i < sizeof(defines) / sizeof(defines[0]); i++)
				if (!strcmp(str, defines[i]))
					return true;
			return false;
		}

		bool checkType(const char *str) {
			static char *types[] = { "FILE", "BITMAPINFO", "BITMAPINFOHEADER", "MSG", "LONG", "Header", "RGBA", "Point", "Rect", "Color", "Font",  "Canvas", "Editor", "Theme", "Syntax", "Lexeme", "Window", "HWND", "HDC", "LRESULT", "UINT", "WPARAM", "LPARAM" };
			for (int i = 0; i < sizeof(types) / sizeof(types[0]); i++)
				if (!strcmp(str, types[i]))
					return true;
			return false;
		}

		void parse(const char *text) {
			count = 0;
			if (!text) return;

			char	tagText	= '\0';
			char	tagComm	= '\0';
			char	last	= '\0';
			int		length	= strlen(text);

			for (int i = 0; i < length; i++) {
				char c = text[i];

				if (c == '\\')
					i++;
				else
					if (c == tagText) {
						lexemeEnd(i + 1);
						tagText = '\0';
					} else
						if ((c == '\r' && (tagComm == '\0' || tagComm == '/')) || (tagComm == '*' && c == '/' && last == '*' && i++)) {
							lexemeEnd(i);
							tagComm = '\0';
						} else
							if (tagText == '\0' && tagComm == '\0')
								if (c == '/' && (text[i + 1] == '/' || text[i + 1] == '*')) {
									tagComm = text[i + 1];
									lexemeBegin(i, Lexeme::ID_COMMENT);
								} else
									if (c == '\'' || c == '"') {
										tagText = c;
										lexemeBegin(i, Lexeme::ID_TEXT);
									} else
										if (c == ' ' || c == '\t')
											lexemeEnd(i);
										else
											if (c >= '0' && c <= '9')
												lexemeBegin(i, Lexeme::ID_NUMBER);
											else
												if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '#')
													lexemeBegin(i, Lexeme::ID_CODE);
												else {
													lexemeEnd(i);
													lexemeBegin(i, Lexeme::ID_CODE);
													lexemeEnd(i + 1);
												}
				last = c;
			}
			lexemeEnd(length);

			printf("lexeme count: %d\n", count);
			for (int i = 0; i < count; i++) {
				Lexeme &lex = lexeme[i];
				char *str = new char[lex.length + 1];
				memcpy(str, &text[lex.offset], lex.length);
				str[lex.length] = '\0';

				if (checkOpcode(str))
					lex.id = Lexeme::ID_OPCODE;
				else
					if (checkDefine(str))
						lex.id = Lexeme::ID_DEFINE;
					else
						if (checkArgument(str))
							lex.id = Lexeme::ID_ARGUMENT;
						else
							if (checkType(str))
								lex.id = Lexeme::ID_TYPE;
			/*
				char *t;
				switch (lex.id) {
					case Lexeme::ID_OPCODE :
						t = "O";
						break;
					case Lexeme::ID_NUMBER :
						t = "N";
						break;
					case Lexeme::ID_TEXT :
						t = "T";
						break;
					case Lexeme::ID_COMMENT :
						t = "/";
						break;
					case Lexeme::ID_ARGUMENT :
						t = "A";
						break;
					default :
						t = "C";
				}

				printf("%d\t%s\t |%s|\n", i, t, str);
			*/
				delete str;
			};

		};

	} syntax;

	struct Cell {
		union {
			struct {
				int	id;
			};
			struct {
				char	c;
				char	fColor;
				char	bColor;
				char	reserved;
			};
		};
	} *cells;
	int	cols, rows;

	ThemeColor	fColor, bColor;
	int			curBuffer;

	Editor(const Theme &theme) : text(NULL), cursor(0, 0), scroll(0, 0), offset(0, 0), valid(false), theme(theme), cells(NULL), cols(0), rows(0), curBuffer(0) {
		FILE *f = fopen("main.cpp", "rb");

		fseek(f, 0, SEEK_END);
		int size = ftell(f);
		fseek(f, 0, SEEK_SET);
		text = (char*)malloc(size + 1);
		fread(text, 1, size, f);
		text[size] = '\0';
		fclose(f);
		syntax.parse(text);
	}

	~Editor() { 
		if (text) free(text);
		if (cells) free(cells);
	}

	void invalidate(const Rect &rect) {
		valid = false;
	}

	void onKey(int key) {
		if (key == VK_LEFT)		cursor.x--;
		if (key == VK_RIGHT)	cursor.x++;
		if (key == VK_UP)		cursor.y--;
		if (key == VK_DOWN)		cursor.y++;
		if (key == VK_BACK)		if (text && strlen(text)) { text[strlen(text) - 1] = '\0'; syntax.parse(text); }
		valid = false;
	};

	void onChar(unsigned char c) {
		if (c < ' ' && c != '\r' && c != '\t')
			return;

		int len = text ? strlen(text) : 0;
		text = (char*)realloc(text, len + 2);
		text[len]		= c;
		text[len + 1]	= 0;

		syntax.parse(text);

		valid = false;
	};

	void onScroll(int x, int y) {
		offset.x += x;
		offset.y += y;
		valid = false;
	}

	void resize(int width, int height) {
		int c = (width + 8) / 9;
		int r = (height + 15) / 16;

		if (cols * rows != c * r) {
			cells = (Cell*)realloc(cells, 2 * c * r * sizeof(cells[0]));
			for (int i = 0; i < 2 * c * r; i++) {
				cells[i].c			= '\0';
				cells[i].bColor		= COLOR_BACK_NORMAL;
				cells[i].bColor		= COLOR_BACK_NORMAL;
				cells[i].reserved	= 0;
			}
			valid = false;
		}
		cols = c;
		rows = r;
	};

	void putChar(int x, int y, unsigned char c) {
		if (x < 0 || y < 0 || x >= cols || y >= rows)
			return;

		Cell &cell = cells[x + y * cols + (curBuffer ? cols * rows : 0)];

		if (cell.c == c && cell.fColor == fColor && cell.bColor == bColor)
			return;

		cell.c		= c;
		cell.fColor	= fColor;
		cell.bColor	= bColor;
	};

	void print(int ox, int &x, int &y, ThemeColor fColor, ThemeColor bColor, const char *text, int length) {
		if (!length) return;

		this->fColor = fColor;
		this->bColor = bColor;

		for (int i = 0; i < length; i++)
			switch (text[i]) {
				case '\n' :
					break;
				case '\r' :
					x = 0;
					y++;
					break;
				case '\t' :
					x = (x / 4 + 1) * 4;
					break;
				default :
					putChar(ox + x, y, text[i]);
					x++;
			}
	}

	void render(Canvas *canvas) {
		if (offset.x) {
		//	canvas->scrollX(offset.x * 9);
			scroll.x += offset.x;
			offset.x = 0;
		}

		if (offset.y) {
		//	canvas->scrollY(offset.y * 16);
			scroll.y += offset.y;
			offset.y = 0;
		}

		if (!valid) {
			curBuffer ^= 1;
			Cell *c = &cells[curBuffer ? cols * rows : 0];
			for (int i = 0; i < cols * rows; i++) {
				c->c		= '\0';
				c->fColor	= COLOR_BACK_NORMAL;
				c->bColor	= COLOR_BACK_NORMAL;
				c->reserved	= 0;
				c++;
			}

			Point pos = Point(scroll.x, scroll.y);

			valid = true;
			int ox = 5;

			if (text) {
				int length = strlen(text);
				int i = 0;
				int lexIndex = 0;
				Syntax::Lexeme *lex = syntax.count ? &syntax.lexeme[0] : NULL;

				while (i < length) {
					if (lex && i == lex->offset) {
						ThemeColor color = (ThemeColor)lex->id;
						print(ox, pos.x, pos.y, color, COLOR_BACK_NORMAL, &text[lex->offset], lex->length);

						i += lex->length;
						lex = ++lexIndex == syntax.count ? NULL : &syntax.lexeme[lexIndex];
					} else {
						print(ox, pos.x, pos.y, COLOR_CODE, COLOR_BACK_NORMAL, &text[i], 1);
						i++;
					}
				}
			}

			print(ox, pos.x, pos.y, COLOR_CURSOR, COLOR_BACK_NORMAL, "\xDD", 1);

			int lines = pos.y - scroll.y + 1;
			char num[4];
			pos = Point(0, scroll.y);
			for (int i = 0; i < lines; i++) {
				itoa(i + 1, num, 10);
				int len = strlen(num);
				pos.x = (3 - len);
				print(0, pos.x, pos.y, COLOR_OPCODE, COLOR_BACK_NORMAL, num, len);
				pos.y++;
			}

			Rect &rect = canvas->rect = {cols, rows, 0, 0};
			//canvas->clear(theme.back_normal);
			for (int r = 0; r < rows; r++)
				for (int c = 0; c < cols; c++) {
					Cell &cell = cells[c + r * cols + (curBuffer ? cols * rows : 0)];
					Cell &last = cells[c + r * cols + (curBuffer ? 0 : cols * rows)];

					if (cell.id != last.id) {
						if (cell.c != '\0')
							canvas->font->putChar(cell.c, theme.byID[cell.fColor], theme.byID[cell.bColor], &canvas->pixels[c * 9 + r * 16 * canvas->width], canvas->width);
						else
							canvas->fill(c * 9, r * 16, 9, 16, theme.byID[cell.bColor]);

						if (c < rect.l)	rect.l = c;
						if (c > rect.r)	rect.r = c;
						if (r < rect.t) rect.t = r;
						if (r > rect.b) rect.b = r;
					}/* else {
						canvas->fill(c * 9, r * 16, 9, 1, theme.byID[COLOR_DEFINE]);
						canvas->fill(c * 9, r * 16 + 15, 9, 1, theme.byID[COLOR_DEFINE]);
						canvas->fill(c * 9, r * 16, 1, 16, theme.byID[COLOR_DEFINE]);
						canvas->fill(c * 9 + 8, r * 16, 1, 16, theme.byID[COLOR_DEFINE]);
					}*/
				}

			rect.b++;
			rect.r++;

			//if (rect.r > rect.l && rect.b > rect.t)
			//	printf("rect: %d %d %d %d\n", rect.l, rect.t, rect.r, rect.b);

			rect.l *= 9;
			rect.r *= 9;
			rect.t *= 16;
			rect.b *= 16;
		}
	}
};

static const Editor::Theme THEME_DARK = {
	0xDADADA, // code
	0xD69D85, // text
	0x4EC9B0, // type
	0xBD63C5, // define
	0xB5CEA8, // number
	0x569CD6, // opcode
	0x57A64A, // comment
	0x7F7F7F, // argument
	0x000000, // back_line
	0x1E1E1E, // back_normal
	0x264F78, // back_selection
	0x653306, // back_search
	0xDCDCDC, // cursor
};

struct Window {
	int		width, height;
	Canvas	*canvas;
	Editor	*editor;

#ifdef WIN32
	HWND	handle;
	HDC		dc;
#endif

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		Window *wnd = (Window*)GetWindowLong(hWnd, GWL_USERDATA);

		switch (msg) {
			case WM_PAINT :
				wnd->paint();
				ValidateRect(hWnd, NULL);
				break;
			case WM_SIZE :
				wnd->resize(LOWORD(lParam), HIWORD(lParam));
				break;
			case WM_KEYDOWN :
				wnd->editor->onKey(wParam);
				wnd->invalidate();
				break;
			case WM_CHAR :
				wnd->editor->onChar(wParam);
				wnd->invalidate();
				break;
			case WM_MOUSEWHEEL :
				wnd->editor->onScroll(0, GET_WHEEL_DELTA_WPARAM(wParam) / 120);
				wnd->invalidate();
				break;
			case WM_LBUTTONDOWN : 
			case WM_RBUTTONDOWN : {
					int x = GET_X_LPARAM(lParam);
					int y = GET_Y_LPARAM(lParam);
				}
				break;
			case WM_DESTROY :
				PostQuitMessage(0);
				break;
			default :
				return DefWindowProc(hWnd, msg, wParam, lParam);
		}
		return 0;
	}

	Window(int width, int height) : width(width), height(height) {
		canvas = new Canvas();
		editor = new Editor(THEME_DARK);
	// init window
		handle = CreateWindow("static", "xedit", WS_OVERLAPPEDWINDOW, 0, 0, width, height, NULL, NULL, NULL, NULL);
		dc = GetDC(handle);
		SetWindowLong(handle, GWL_USERDATA, (LONG)this);
		SetWindowLong(handle, GWL_WNDPROC, (LONG)&WndProc);
		ShowWindow(handle, SW_SHOWDEFAULT);
	}

	~Window() {
		delete editor;
		delete canvas;
		ReleaseDC(handle, dc);
		DestroyWindow(handle);
	}

	void close() {
		PostQuitMessage(0);
	}

	void invalidate() {
		InvalidateRect(handle, NULL, false);
	}

	void resize(int width, int height) {
		editor->resize(width, height);
		canvas->resize(editor->cols * 9, editor->rows * 16);
	}

	void loop() {
		MSG msg;
		while (GetMessage(&msg, 0, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);			
		}
	}

	void paint() {
		editor->render(canvas);
		canvas->present(dc);
	}
};

Window *window;

int main() {
//	convertFont("font.tga", "font.dat");
	window = new Window(800, 600);
	window->loop();
	delete window;
	return 0;
};