  # Python 环境测试：在终端输出一个彩色渐变爱心。
import colorsys

heart = """
  **     **
 ****   ****
****** ******
*************
 ***********
  *********
   *******
    *****
     ***
      *
"""

stars = sum(row.count("*") for row in heart.splitlines())
star_index = 0
colored_rows = []

for row in heart.splitlines():
    colored_row = []
    for character in row:
        if character == "*":
            hue = star_index / stars
            red, green, blue = (
                round(channel * 255)
                for channel in colorsys.hsv_to_rgb(hue, 0.85, 1.0)
            )
            colored_row.append(f"\033[38;2;{red};{green};{blue}m{character}\033[0m")
            star_index += 1
        else:
            colored_row.append(character)
    colored_rows.append("".join(colored_row))

print("\n".join(colored_rows))
print("Python 环境运行正常！")
